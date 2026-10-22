"""The .gcno bundle: manifest v2 plus notes (and, on request, sources).

The bundle is produced where the checkout exists and consumed where it does
not (the presubmit analyzer, or a developer rendering CI artifacts). The
producer therefore resolves every recorded source path once and ships the
result in `manifest.json`:

    {
      "schema_version": 2,
      "repo_root": "/abs/path/of/gamesdk/at/build/time",
      "files": {"<recorded path>": "<repo-relative resolved path>", ...},
      "notes": [{"member": "notes/...gcno", "stem": "Foo.cpp", "stamp": "hex"}],
      "stats": {"external_files": N, "excluded_files": N, "unknown_files": []}
    }

A consumer pairs .gcda data with the notes, runs llvm-cov, and looks every
recorded path up in `files`; anything absent is not SDK code.
"""

from collections.abc import Sequence
from dataclasses import dataclass
import io
import json
from pathlib import Path
import posixpath
import tarfile
from typing import Any

from agdk_coverage import gcov, paths

SCHEMA_VERSION = 2
BUNDLE_FILENAME = "agdk_coverage_bundle.tar.gz"
MANIFEST_MEMBER = "manifest.json"
CHANGED_LINES_MEMBER = "git_changed_lines.json"
_NOTES_PREFIX = "notes/"
_SOURCES_PREFIX = "sources/"


@dataclass(frozen=True)
class Bundle:
    """An extracted bundle."""

    manifest: dict[str, Any]
    notes_dir: Path
    sources_dir: Path | None


class ManifestMapper:
    """Maps recorded paths through the producer's `files` table."""

    def __init__(self, files: dict[str, str]) -> None:
        self._files = {posixpath.normpath(k): v for k, v in files.items()}

    def __call__(self, recorded: str) -> tuple[str, str | None]:
        canonical = self._files.get(posixpath.normpath(recorded.strip()))
        if canonical is None:
            return paths.EXTERNAL, None
        return paths.OK, canonical


def write(
    bundle_path: Path,
    repo: Path,
    notes: Sequence[gcov.Note],
    llvm_cov: Path,
    mapper: gcov.SourceMapper,
    with_sources: bool,
    diff_base: str | None = None,
    changed_lines: dict[str, set[int]] | None = None,
) -> dict[str, Any]:
    """Writes the bundle and returns its manifest.

    Runs a notes-only `llvm-cov gcov` pass to learn every recorded source
    path up front, so the consumer never has to map paths itself.
    """
    if not notes:
        raise ValueError("no .gcno notes to bundle")
    files: dict[str, str] = {}
    statuses: dict[str, str] = {}
    for parsed in gcov.run_llvm_cov(llvm_cov, [(n.path, None) for n in notes]):
        for recorded in parsed:
            if recorded in statuses:
                continue
            status, canonical = mapper(recorded)
            statuses[recorded] = status
            if status == paths.OK and canonical is not None:
                files[recorded] = canonical

    repo_root = repo.resolve()
    manifest_notes = []
    bundle_path.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(bundle_path, "w:gz") as tar:
        for index, note in enumerate(notes):
            try:
                rel = note.path.resolve().relative_to(repo_root).as_posix()
            except ValueError:
                rel = f"host/{index}/{note.path.name}"
            member = _NOTES_PREFIX + rel
            tar.add(note.path, arcname=member)
            manifest_notes.append({"member": member, "stem": note.stem, "stamp": note.stamp})
        if with_sources:
            for canonical in sorted(set(files.values())):
                tar.add(repo_root / canonical, arcname=_SOURCES_PREFIX + canonical)
        manifest: dict[str, Any] = {
            "schema_version": SCHEMA_VERSION,
            "repo_root": str(repo_root),
            "files": dict(sorted(files.items())),
            "notes": manifest_notes,
            "stats": {
                "external_files": sum(1 for s in statuses.values() if s == paths.EXTERNAL),
                "excluded_files": sum(1 for s in statuses.values() if s == paths.EXCLUDED),
                "unknown_files": sorted(r for r, s in statuses.items() if s == paths.UNKNOWN),
            },
        }
        if diff_base is not None or changed_lines is not None:
            sorted_changed = {
                path: sorted(lines)
                for path, lines in sorted((changed_lines or {}).items())
            }
            manifest["diff_base"] = diff_base
            manifest["changed_lines"] = sorted_changed
            diff_payload = json.dumps(
                {"diff_base": diff_base, "changed_lines": sorted_changed},
                indent=2,
                sort_keys=True,
            ).encode("utf-8")
            diff_info = tarfile.TarInfo(CHANGED_LINES_MEMBER)
            diff_info.size = len(diff_payload)
            tar.addfile(diff_info, io.BytesIO(diff_payload))
        payload = json.dumps(manifest, indent=2, sort_keys=True).encode("utf-8")
        info = tarfile.TarInfo(MANIFEST_MEMBER)
        info.size = len(payload)
        tar.addfile(info, io.BytesIO(payload))
    return manifest


def read(bundle_path: Path, dest: Path) -> Bundle:
    """Extracts `bundle_path` into `dest` and validates the manifest."""
    with tarfile.open(bundle_path, "r:*") as tar:
        tar.extractall(path=dest, filter="data")
    manifest_path = dest / MANIFEST_MEMBER
    if not manifest_path.is_file():
        raise ValueError(f"{bundle_path} has no {MANIFEST_MEMBER}; not a v2 bundle")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    version = manifest.get("schema_version")
    if version != SCHEMA_VERSION:
        raise ValueError(f"{bundle_path}: unsupported schema_version {version!r}")
    sources = dest / _SOURCES_PREFIX.rstrip("/")
    return Bundle(manifest, dest / _NOTES_PREFIX.rstrip("/"), sources if sources.is_dir() else None)
