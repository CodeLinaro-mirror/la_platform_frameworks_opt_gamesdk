"""Writes coverage_summary.json (schema v2) and the HTML report."""

from collections.abc import Callable, Mapping
import html
import json
from pathlib import Path
import re
from typing import Any

from agdk_coverage import model

SCHEMA_VERSION = 2
SUMMARY_FILENAME = "coverage_summary.json"
_TEMPLATES = Path(__file__).resolve().parent / "templates"
_INDEX_TEMPLATE = _TEMPLATES / "index.html"
_ROW_TEMPLATE_RE = re.compile(
    r"<!-- TABLE_ROW_TEMPLATE_START(.*?)TABLE_ROW_TEMPLATE_END -->", re.DOTALL
)
# Coverage badge thresholds used by the index page.
_HIGH_PCT = 75.0
_MEDIUM_PCT = 50.0

SourceReader = Callable[[str], str | None]


def summary_payload(
    coverage: model.CoverageSet,
    diff_base: str | None,
    gcov_stats: Mapping[str, Any] | None,
) -> dict[str, Any]:
    """The coverage_summary.json document; `files` keys are repo-relative."""
    return {
        "schema_version": SCHEMA_VERSION,
        "diff_base": diff_base,
        "overall": model.totals(coverage.rows()).as_dict(),
        "cpp": model.totals(coverage.rows(model.CPP)).as_dict(),
        "java": model.totals(coverage.rows(model.JAVA)).as_dict(),
        "gcov": dict(gcov_stats or {}),
        "files": {
            row.path: {
                "language": row.language,
                "lines": {str(ln): count for ln, count in sorted(row.lines.items())},
                "changed_lines": sorted(row.changed_lines),
            }
            for row in coverage.rows()
        },
    }


def write_summary_json(
    out_dir: Path,
    coverage: model.CoverageSet,
    diff_base: str | None,
    gcov_stats: Mapping[str, Any] | None = None,
) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / SUMMARY_FILENAME
    payload = summary_payload(coverage, diff_base, gcov_stats)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return path


def detail_filename(path: str) -> str:
    """Per-file page name: directory separators become `__`."""
    return "sources/" + path.replace("/", "__").replace("..", "_") + ".html"


def _badge(pct: float) -> tuple[str, str, str]:
    if pct >= _HIGH_PCT:
        return "fill-green", "badge-success", "High"
    if pct >= _MEDIUM_PCT:
        return "fill-yellow", "badge-warning", "Medium"
    return "fill-red", "badge-danger", "Low"


def _render_row(row: model.FileCoverage, template: str) -> str:
    totals = model.totals([row])
    fill, badge, label = _badge(totals.pct)
    if totals.diff_pct is not None:
        diff_cell = (
            f"<strong>{totals.diff_pct:.1f}%</strong>"
            f" ({totals.diff_executed_lines}/{totals.diff_total_lines})"
        )
    elif row.changed_lines:
        diff_cell = "N/A (non-exec)"
    else:
        diff_cell = "&mdash;"
    link = f'<a href="{html.escape(detail_filename(row.path), quote=True)}">{html.escape(row.path)}</a>'
    return (
        template.replace("{{ITEM_NAME_HTML}}", link)
        .replace("{{TOTAL_LOC}}", f"{totals.total_lines:,}")
        .replace("{{EXEC_LOC}}", f"{totals.executed_lines:,}")
        .replace("{{COVERAGE_PCT}}", f"{totals.pct:.1f}")
        .replace("{{DIFF_COVERAGE_CELL}}", diff_cell)
        .replace("{{DATA_CHANGED}}", "true" if row.changed_lines else "false")
        .replace("{{DATA_LANG}}", html.escape(row.language, quote=True))
        .replace("{{FILL_CLASS}}", fill)
        .replace("{{BADGE_CLASS}}", badge)
        .replace("{{BADGE_LABEL}}", label)
    )


def _render_index(coverage: model.CoverageSet) -> str:
    raw = _INDEX_TEMPLATE.read_text(encoding="utf-8")
    match = _ROW_TEMPLATE_RE.search(raw)
    if match is None:
        raise ValueError(f"{_INDEX_TEMPLATE} lacks the TABLE_ROW_TEMPLATE block")
    row_template = match.group(1).strip()
    page = _ROW_TEMPLATE_RE.sub("", raw).strip()
    cpp = model.totals(coverage.rows(model.CPP))
    java = model.totals(coverage.rows(model.JAVA))
    overall = model.totals(coverage.rows())
    if overall.diff_pct is not None:
        diff_pct, diff_text = (
            f"{overall.diff_pct:.1f}%",
            f"{overall.diff_executed_lines:,} / {overall.diff_total_lines:,} modified lines executed",
        )
    else:
        diff_pct, diff_text = "N/A", "No modified executable lines"
    if java.total_lines:
        java_pct, java_text = (
            f"{java.pct:.1f}%",
            f"{java.executed_lines:,} / {java.total_lines:,} JVM lines executed",
        )
    else:
        java_pct, java_text = "N/A", "No JaCoCo XML reports ingested"
    rows = "\n".join(_render_row(row, row_template) for row in coverage.rows())
    return (
        page.replace("{{OVERALL_PCT}}", f"{cpp.pct:.1f}")
        .replace("{{EXEC_LOC}}", f"{cpp.executed_lines:,}")
        .replace("{{TOTAL_LOC}}", f"{cpp.total_lines:,}")
        .replace("{{DIFF_PCT}}", diff_pct)
        .replace("{{DIFF_SUBTEXT}}", diff_text)
        .replace("{{JAVA_PCT}}", java_pct)
        .replace("{{JAVA_SUBTEXT}}", java_text)
        .replace("{{NUM_FILES}}", str(len(coverage)))
        .replace("{{TABLE_ROWS}}", rows)
    )


def _render_source(row: model.FileCoverage, source_text: str) -> str:
    template = (_TEMPLATES / "source.html").read_text(encoding="utf-8")
    lines = []
    for number, text in enumerate(source_text.splitlines(), start=1):
        count = row.lines.get(number)
        if count is None:
            cls, count_text = "line-neutral", ""
        elif count > 0:
            cls, count_text = "line-covered", f"{count}x"
        else:
            cls, count_text = "line-missed", "0x"
        changed = number in row.changed_lines
        if changed:
            cls += " line-changed"
        lines.append(
            f'<tr class="{cls}" data-changed="{"true" if changed else "false"}">'
            f'<td class="ln">{"&Delta; " if changed else ""}{number}</td>'
            f'<td class="cnt">{count_text}</td>'
            f'<td class="code"><pre>{html.escape(text)}</pre></td></tr>'
        )
    totals = model.totals([row])
    meta = (
        f"Coverage: <strong>{totals.pct:.1f}%</strong>"
        f" ({totals.executed_lines:,} / {totals.total_lines:,} executable lines)"
    )
    if totals.diff_pct is not None:
        meta += (
            f" &middot; CL Diff (&Delta;): <strong>{totals.diff_pct:.1f}%</strong>"
            f" ({totals.diff_executed_lines:,} / {totals.diff_total_lines:,} lines)"
        )
    return (
        template.replace("{{TITLE}}", html.escape(row.path))
        .replace("{{META}}", meta)
        .replace("{{ROWS}}", "\n".join(lines))
    )


def write_html(out_dir: Path, coverage: model.CoverageSet, read_source: SourceReader) -> Path:
    """Writes index.html and one sources/ page per file; returns the index."""
    out_dir.mkdir(parents=True, exist_ok=True)
    index = out_dir / "index.html"
    index.write_text(_render_index(coverage), encoding="utf-8")
    for row in coverage.rows():
        text = read_source(row.path)
        if text is None:
            text = f"// Source file not found: {row.path}\n"
        page = out_dir / detail_filename(row.path)
        page.parent.mkdir(parents=True, exist_ok=True)
        page.write_text(_render_source(row, text), encoding="utf-8")
    return index
