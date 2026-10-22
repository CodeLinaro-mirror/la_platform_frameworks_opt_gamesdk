"""Line coverage model shared by the gcov and JaCoCo readers and the reports."""

from collections.abc import Iterable, Mapping
from dataclasses import dataclass, field, replace
from typing import Any

# Languages a FileCoverage row may carry; the summary groups totals by them.
CPP = "cpp"
JAVA = "java"


@dataclass(frozen=True)
class FileCoverage:
    """Executed-line counts of one repository-relative source file."""

    path: str
    language: str
    lines: Mapping[int, int]
    changed_lines: frozenset[int] = field(default_factory=frozenset)

    @property
    def total_lines(self) -> int:
        return len(self.lines)

    @property
    def executed_lines(self) -> int:
        return sum(1 for count in self.lines.values() if count > 0)

    @property
    def diff_lines(self) -> list[int]:
        """Changed lines that are executable."""
        return sorted(line for line in self.changed_lines if line in self.lines)

    @property
    def diff_executed_lines(self) -> int:
        return sum(1 for line in self.diff_lines if self.lines[line] > 0)


@dataclass(frozen=True)
class Totals:
    """Aggregate line and incremental (changed-line) coverage."""

    total_lines: int = 0
    executed_lines: int = 0
    diff_total_lines: int = 0
    diff_executed_lines: int = 0

    @property
    def pct(self) -> float:
        if self.total_lines == 0:
            return 0.0
        return self.executed_lines / self.total_lines * 100.0

    @property
    def diff_pct(self) -> float | None:
        if self.diff_total_lines == 0:
            return None
        return self.diff_executed_lines / self.diff_total_lines * 100.0

    def as_dict(self) -> dict[str, Any]:
        return {
            "total_lines": self.total_lines,
            "executed_lines": self.executed_lines,
            "pct": round(self.pct, 2),
            "diff_total_lines": self.diff_total_lines,
            "diff_executed_lines": self.diff_executed_lines,
            "diff_pct": (
                None if self.diff_pct is None else round(self.diff_pct, 2)
            ),
        }


def totals(rows: Iterable[FileCoverage]) -> Totals:
    """Sums the per-file counts of `rows`."""
    acc = Totals()
    for row in rows:
        acc = Totals(
            total_lines=acc.total_lines + row.total_lines,
            executed_lines=acc.executed_lines + row.executed_lines,
            diff_total_lines=acc.diff_total_lines + len(row.diff_lines),
            diff_executed_lines=acc.diff_executed_lines
            + row.diff_executed_lines,
        )
    return acc


class CoverageSet:
    """Per-file coverage keyed by repository-relative path.

    Adding the same file twice sums the hit counts line by line, which is the
    union of all test runs that executed it.
    """

    def __init__(self) -> None:
        self._files: dict[str, FileCoverage] = {}

    def add(self, path: str, language: str, lines: Mapping[int, int]) -> None:
        """Adds or merges a row; a file without executable lines is ignored."""
        if not path:
            raise ValueError("coverage row without a path")
        if not lines:
            return
        existing = self._files.get(path)
        if existing is None:
            self._files[path] = FileCoverage(path, language, dict(lines))
            return
        if existing.language != language:
            raise ValueError(
                f"{path}: language {language} conflicts with"
                f" {existing.language}"
            )
        merged = dict(existing.lines)
        for line, count in lines.items():
            merged[line] = merged.get(line, 0) + count
        self._files[path] = replace(existing, lines=merged)

    def with_changed_lines(
        self, changed: Mapping[str, Iterable[int]]
    ) -> "CoverageSet":
        """Returns a copy annotated with the changed lines of each file."""
        annotated = CoverageSet()
        for path, row in self._files.items():
            annotated._files[path] = replace(
                row, changed_lines=frozenset(changed.get(path, ()))
            )
        return annotated

    def rows(self, language: str | None = None) -> list[FileCoverage]:
        """Rows sorted by path, optionally restricted to one language."""
        return [
            row
            for _, row in sorted(self._files.items())
            if language is None or row.language == language
        ]

    def __len__(self) -> int:
        return len(self._files)

    def __contains__(self, path: str) -> bool:
        return path in self._files

    def __getitem__(self, path: str) -> FileCoverage:
        return self._files[path]
