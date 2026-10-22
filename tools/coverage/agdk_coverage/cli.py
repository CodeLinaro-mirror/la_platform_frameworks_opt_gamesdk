"""Command line of the AGDK coverage tool: `report` and `bundle`."""

import argparse
from collections.abc import Sequence
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from typing import Any

from agdk_coverage import bundle, gcov, gitdiff, jacoco, model, paths, report

# tools/coverage/agdk_coverage/cli.py -> the gamesdk checkout.
DEFAULT_REPO = Path(__file__).resolve().parents[3]


def find_llvm_cov(explicit: Path | None, repo: Path) -> Path:
    """`--llvm-cov`, then PATH, then the NDK prebuilts next to the checkout."""
    if explicit is not None:
        if not explicit.is_file():
            raise FileNotFoundError(f"llvm-cov not found at {explicit}")
        return explicit
    on_path = shutil.which("llvm-cov")
    if on_path:
        return Path(on_path)
    candidates = sorted(
        (repo.parent / "prebuilts" / "ndk").glob(
            "*/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-cov"
        )
    )
    if candidates:
        return candidates[-1]
    raise FileNotFoundError("llvm-cov not on PATH and no NDK under prebuilts/; pass --llvm-cov")


def _add_common(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--repo", type=Path, default=DEFAULT_REPO, help="gamesdk checkout")
    parser.add_argument("--llvm-cov", type=Path, default=None, help="llvm-cov binary")
    parser.add_argument("--diff-base", default=None, help="git revision for incremental coverage")
    parser.add_argument(
        "--native-build-dir",
        dest="native_build_dirs",
        action="append",
        type=Path,
        default=[],
        help="directory with .gcno/.gcda files (default: every module .cxx tree)",
    )


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="agdk_coverage", description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    rep = subparsers.add_parser("report", help="write coverage_summary.json and HTML")
    _add_common(rep)
    rep.add_argument("--out", type=Path, required=True, help="output directory")
    rep.add_argument(
        "--build-root",
        default=None,
        help="repository root recorded in the notes when they were compiled"
        " elsewhere (default: --repo)",
    )
    rep.add_argument(
        "--gcda-dir",
        dest="gcda_dirs",
        action="append",
        type=Path,
        default=[],
        help="directory or zip with pulled .gcda files (repeatable)",
    )
    rep.add_argument("--bundle", type=Path, default=None, help=f"{bundle.BUNDLE_FILENAME} to report from")
    rep.add_argument(
        "--jacoco-xml",
        dest="jacoco_xmls",
        action="append",
        type=Path,
        default=[],
        help="JaCoCo XML report (repeatable)",
    )

    bun = subparsers.add_parser("bundle", help=f"write {bundle.BUNDLE_FILENAME}")
    _add_common(bun)
    bun.add_argument("--out", type=Path, required=True, help="bundle path")
    bun.add_argument("--with-sources", action="store_true", help="include the covered sources")
    return parser.parse_args(argv)


def _native_dirs(args: argparse.Namespace, repo: Path) -> list[Path]:
    return [d.resolve() for d in args.native_build_dirs] or paths.native_build_dirs(repo)


def _changed_lines(repo: Path, base: str | None) -> dict[str, set[int]] | None:
    """Lines changed since `base`, or None (with a warning) when git cannot diff."""
    if not base:
        return None
    try:
        return gitdiff.changed_lines(repo, base)
    except (ValueError, subprocess.SubprocessError) as err:
        print(f"warning: incremental coverage disabled: {err}", file=sys.stderr)
        return None


def _gcov_stats(stats: gcov.Stats) -> dict[str, Any]:
    return {
        "notes": stats.notes,
        "units": stats.units,
        "gcda_files": stats.gcda_files,
        "paired_gcda": stats.paired_gcda,
        "unmatched_gcda": stats.unmatched_gcda,
        "external_files": stats.external_files,
        "excluded_files": stats.excluded_files,
        "unknown_files": list(stats.unknown_files),
    }


def run_report(args: argparse.Namespace) -> int:
    repo = args.repo.resolve()
    llvm_cov = find_llvm_cov(args.llvm_cov, repo)
    coverage = model.CoverageSet()
    with tempfile.TemporaryDirectory(prefix="agdk_bundle_") as tmp:
        if args.bundle is not None:
            extracted = bundle.read(args.bundle, Path(tmp))
            notes = gcov.find_notes([extracted.notes_dir])
            mapper: gcov.SourceMapper = bundle.ManifestMapper(extracted.manifest["files"])
            data_dirs = list(args.gcda_dirs)
            sources_root = extracted.sources_dir or repo
            bundled_diff_base = extracted.manifest.get("diff_base")
            bundled_changed = extracted.manifest.get("changed_lines")
        else:
            native_dirs = _native_dirs(args, repo)
            notes = gcov.find_notes(native_dirs)
            mapper = paths.SourceMapper(args.build_root or str(repo), repo)
            data_dirs = [*native_dirs, *args.gcda_dirs]
            sources_root = repo
            bundled_diff_base = None
            bundled_changed = None
        result = gcov.collect(notes, gcov.find_data(data_dirs), llvm_cov, mapper)
        for path, lines in result.files.items():
            coverage.add(path, model.CPP, lines)
        jvm_files, jvm_unknown = jacoco.parse(args.jacoco_xmls, repo, paths.jvm_source_roots(repo))
        for path, lines in jvm_files.items():
            coverage.add(path, model.JAVA, lines)
        changed = _changed_lines(repo, args.diff_base)
        diff_base = args.diff_base if changed is not None else bundled_diff_base
        if changed is not None:
            coverage = coverage.with_changed_lines(changed)
        elif bundled_changed:
            coverage = coverage.with_changed_lines(
                {path: set(lines) for path, lines in bundled_changed.items()}
            )

        def read_source(path: str) -> str | None:
            candidate = sources_root / path
            if not candidate.is_file():
                return None
            return candidate.read_text(encoding="utf-8", errors="replace")

        stats = _gcov_stats(result.stats)
        if args.bundle is not None:
            stats.update(extracted.manifest["stats"])
        report.write_summary_json(args.out, coverage, diff_base, stats)
        index = report.write_html(args.out, coverage, read_source)

    unknown_gcov = list(stats["unknown_files"])
    overall = model.totals(coverage.rows())
    print(
        f"{len(coverage)} files, {overall.executed_lines}/{overall.total_lines} lines executed"
        f" ({overall.pct:.1f}%); gcov: {stats['notes']} notes, {stats['units']} units,"
        f" {stats['paired_gcda']}/{stats['gcda_files']} gcda paired,"
        f" {len(unknown_gcov)} unknown files; jacoco: {len(jvm_unknown)} unknown files"
    )
    for unknown in [*unknown_gcov, *jvm_unknown]:
        print(f"warning: unmapped source {unknown}", file=sys.stderr)
    print(f"Report: file://{index}")
    return 0


def run_bundle(args: argparse.Namespace) -> int:
    repo = args.repo.resolve()
    llvm_cov = find_llvm_cov(args.llvm_cov, repo)
    notes = gcov.find_notes(_native_dirs(args, repo))
    changed = _changed_lines(repo, args.diff_base)
    manifest = bundle.write(
        args.out,
        repo,
        notes,
        llvm_cov,
        paths.SourceMapper(str(repo), repo),
        args.with_sources,
        diff_base=args.diff_base if changed is not None else None,
        changed_lines=changed,
    )
    stats = manifest["stats"]
    print(
        f"Bundled {len(notes)} notes covering {len(set(manifest['files'].values()))} files"
        f" into {args.out}; {len(stats['unknown_files'])} unknown files"
    )
    for unknown in stats["unknown_files"]:
        print(f"warning: unmapped source {unknown}", file=sys.stderr)
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        if args.command == "report":
            return run_report(args)
        return run_bundle(args)
    except (FileNotFoundError, ValueError, RuntimeError) as err:
        print(f"error: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
