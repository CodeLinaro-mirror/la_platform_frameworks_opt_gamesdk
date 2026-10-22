"""Pairs .gcno notes with .gcda data and runs `llvm-cov gcov -i` on them.

A compilation unit is identified by its stamp (the CFG checksum clang
stores in both the note and the data file) together with the repository
files it covers. The same translation unit compiled into several CMake
targets, or reached through a symlink, therefore collapses into one unit,
and every .gcda file with a matching stem and stamp is applied to it exactly
once.
"""

from collections.abc import Callable, Iterable, Mapping, Sequence
from dataclasses import dataclass
import hashlib
from pathlib import Path
import subprocess
import tempfile
import zipfile

from agdk_coverage import paths

# `llvm-cov gcov -i` over the ~100 notes of a module build finishes in a few
# seconds; the limit only bounds a hung toolchain.
LLVM_COV_TIMEOUT_SECONDS = 300
# GCOV files start with a 4-byte magic and a 4-byte version, followed by the
# 32-bit stamp shared between a note and its data files.
_STAMP = slice(8, 12)
_HEADER_LEN = 12

# Maps a recorded source path to (status, canonical repo-relative path).
SourceMapper = Callable[[str], tuple[str, str | None]]
DataKey = tuple[str, str]  # (stem, stamp)


@dataclass(frozen=True)
class Note:
    """A .gcno file; `stem` is the source name (`Foo.cpp` for `Foo.cpp.gcno`)."""

    path: Path
    stem: str
    stamp: str


@dataclass(frozen=True)
class Stats:
    """What the gcov pass saw; the report surfaces these counters.

    `paired_gcda` payloads were applied to an SDK unit, `unmatched_gcda`
    payloads matched no note at all (stale or foreign data); the remainder
    belongs to notes without SDK files, such as test sources.
    """

    notes: int
    units: int
    gcda_files: int
    paired_gcda: int
    unmatched_gcda: int
    external_files: int
    excluded_files: int
    unknown_files: tuple[str, ...]


@dataclass(frozen=True)
class GcovResult:
    files: dict[str, dict[int, int]]
    stats: Stats


def read_stamp(data: bytes) -> str:
    if len(data) < _HEADER_LEN:
        raise ValueError("truncated GCOV header")
    return data[_STAMP].hex()


def find_notes(dirs: Iterable[Path]) -> list[Note]:
    """All .gcno notes below `dirs`, sorted by path."""
    notes = []
    for gcno in sorted(p for d in dirs if d.is_dir() for p in d.rglob("*.gcno")):
        with gcno.open("rb") as f:
            header = f.read(_HEADER_LEN)
        notes.append(Note(gcno, gcno.name.removesuffix(".gcno"), read_stamp(header)))
    return notes


def find_data(dirs: Iterable[Path]) -> dict[DataKey, list[bytes]]:
    """Collects .gcda payloads from `dirs` and the zip archives below them.

    Payloads are keyed by (stem, stamp) and deduplicated by content, so a
    file that was both pulled from a device and copied next to a note counts
    once.
    """
    payloads: dict[DataKey, list[bytes]] = {}
    seen: set[str] = set()

    def add(name: str, data: bytes) -> None:
        if len(data) < _HEADER_LEN:
            raise ValueError(f"truncated .gcda payload: {name}")
        digest = hashlib.sha256(data).hexdigest()
        if digest in seen:
            return
        seen.add(digest)
        key = (Path(name).name.removesuffix(".gcda"), read_stamp(data))
        payloads.setdefault(key, []).append(data)

    for directory in dirs:
        if directory.is_file() and directory.suffix == ".zip":
            archives, files = [directory], []
        else:
            archives = sorted(directory.rglob("*.zip"))
            files = sorted(directory.rglob("*.gcda"))
        for gcda in files:
            add(gcda.name, gcda.read_bytes())
        for archive in archives:
            with zipfile.ZipFile(archive) as zf:
                for member in zf.namelist():
                    if member.endswith(".gcda"):
                        add(member, zf.read(member))
    return payloads


def parse_intermediate(text: str) -> dict[str, dict[int, int]]:
    """Parses `llvm-cov gcov -i` output into {recorded path: {line: count}}."""
    files: dict[str, dict[int, int]] = {}
    current: dict[int, int] | None = None
    for line in text.splitlines():
        if line.startswith("file:"):
            current = files.setdefault(line[len("file:"):], {})
        elif line.startswith("lcount:") and current is not None:
            line_no, count = line[len("lcount:"):].split(",")[:2]
            current[int(line_no)] = current.get(int(line_no), 0) + int(count)
    return files


def run_llvm_cov(
    llvm_cov: Path, pairs: Sequence[tuple[Path, bytes | None]]
) -> list[dict[str, dict[int, int]]]:
    """Runs one `llvm-cov gcov -i` batch; returns the parsed output per pair.

    Every pair is staged under a unique name because llvm-cov writes
    `<input>.gcov` into the working directory and the same source name
    occurs in several targets.
    """
    if not pairs:
        return []
    with tempfile.TemporaryDirectory(prefix="agdk_gcov_") as tmp:
        work = Path(tmp)
        staged = []
        for index, (note, gcda) in enumerate(pairs):
            note_copy = work / f"u_{index}.gcno"
            note_copy.write_bytes(note.read_bytes())
            if gcda is not None:
                (work / f"u_{index}.gcda").write_bytes(gcda)
            staged.append(note_copy.name)
        result = subprocess.run(
            [str(llvm_cov), "gcov", "-i", *staged],
            cwd=work,
            capture_output=True,
            text=True,
            check=False,
            timeout=LLVM_COV_TIMEOUT_SECONDS,
        )
        if result.returncode != 0:
            raise RuntimeError(
                f"llvm-cov gcov failed ({result.returncode}): {result.stderr}"
            )
        return [
            parse_intermediate(
                (work / f"{name}.gcov").read_text(encoding="utf-8", errors="replace")
            )
            for name in staged
        ]


def collect(
    notes: Sequence[Note],
    data: Mapping[DataKey, Sequence[bytes]],
    llvm_cov: Path,
    mapper: SourceMapper,
) -> GcovResult:
    """Line counts per repository file from `notes` and their `data`.

    A notes-only pass maps every note to the repository files it covers;
    notes with the same stamp and file set are one unit. The second pass
    applies each matching .gcda payload once to its unit and sums the counts.
    """
    classified: dict[str, tuple[str, str | None]] = {}

    def classify(recorded: str) -> tuple[str, str | None]:
        if recorded not in classified:
            classified[recorded] = mapper(recorded)
        return classified[recorded]

    units: dict[tuple[str, tuple[str, ...]], Note] = {}
    for note, parsed in zip(notes, run_llvm_cov(llvm_cov, [(n.path, None) for n in notes])):
        covered = {
            canonical
            for recorded, lines in parsed.items()
            if lines
            for status, canonical in [classify(recorded)]
            if status == paths.OK and canonical is not None
        }
        if covered:
            units.setdefault((note.stamp, tuple(sorted(covered))), note)

    pairs: list[tuple[Path, bytes | None]] = []
    paired_gcda = 0
    for (stamp, _), note in units.items():
        payloads = data.get((note.stem, stamp), ())
        paired_gcda += len(payloads)
        pairs.extend((note.path, payload) for payload in payloads)
        if not payloads:
            pairs.append((note.path, None))

    files: dict[str, dict[int, int]] = {}
    for parsed in run_llvm_cov(llvm_cov, pairs):
        for recorded, lines in parsed.items():
            status, canonical = classify(recorded)
            # A file: record without lcount lines (a header whose functions
            # were all inlined away) carries no coverage.
            if status != paths.OK or canonical is None or not lines:
                continue
            target = files.setdefault(canonical, {})
            for line_no, count in lines.items():
                target[line_no] = target.get(line_no, 0) + count

    statuses = [status for status, _ in classified.values()]
    known_keys = {(note.stem, note.stamp) for note in notes}
    return GcovResult(
        files=files,
        stats=Stats(
            notes=len(notes),
            units=len(units),
            gcda_files=sum(len(v) for v in data.values()),
            paired_gcda=paired_gcda,
            unmatched_gcda=sum(len(v) for k, v in data.items() if k not in known_keys),
            external_files=statuses.count(paths.EXTERNAL),
            excluded_files=statuses.count(paths.EXCLUDED),
            unknown_files=tuple(
                sorted(r for r, (s, _) in classified.items() if s == paths.UNKNOWN)
            ),
        ),
    )
