#!/usr/bin/env python3
"""Fails a CI build whose coverage artifacts are empty or miss a module.

A path-mapping or instrumentation regression in the coverage pipeline
produces an empty report instead of an error, and the build stays green.
build.sh runs this check on coverage_summary.json after the host report and
on the .gcno bundle after the device test APK build.

Usage:
    check_coverage_artifacts.py summary <coverage_summary.json> \
        --language cpp --language java
    check_coverage_artifacts.py bundle <bundle.tar.gz> \
        --module games-frame-pacing --module game-activity
"""

import argparse
from collections.abc import Sequence
import json
from pathlib import Path
import sys
import tarfile


def _executed_lines(info: dict[str, object]) -> int:
    """Executed lines of one summary file entry (schema 2 `lines` map)."""
    lines = info.get("lines")
    if isinstance(lines, dict):
        return sum(
            1
            for count in lines.values()
            if isinstance(count, int) and not isinstance(count, bool) and count > 0
        )
    executed = info.get("executed_lines")
    if isinstance(executed, int) and not isinstance(executed, bool):
        return executed
    return 0


def check_summary(summary_path: Path, languages: Sequence[str]) -> list[str]:
    """Returns one problem per language without an executed source line."""
    with summary_path.open(encoding="utf-8") as f:
        files = json.load(f).get("files", {})
    problems = []
    for language in languages:
        rows = [
            info for info in files.values()
            if info.get("language") == language
        ]
        if not rows:
            problems.append(f"{summary_path}: no {language} files")
        elif all(_executed_lines(info) == 0 for info in rows):
            problems.append(
                f"{summary_path}: {len(rows)} {language} files, none with an"
                " executed line"
            )
    return problems


def check_bundle(bundle_path: Path, modules: Sequence[str]) -> list[str]:
    """Returns one problem per module without a .gcno note in the bundle."""
    with tarfile.open(bundle_path, "r:*") as tar:
        notes = [m.name for m in tar.getmembers() if m.name.endswith(".gcno")]
    return [
        f"{bundle_path}: no .gcno notes under {module}/.cxx/"
        for module in modules
        if not any(f"{module}/.cxx/" in name for name in notes)
    ]


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n", 1)[0])
    subparsers = parser.add_subparsers(dest="artifact", required=True)
    summary = subparsers.add_parser("summary", help="check coverage_summary.json")
    summary.add_argument("path", type=Path)
    summary.add_argument(
        "--language", action="append", default=[], required=True,
        help="language that must have an executed line (repeatable)",
    )
    bundle = subparsers.add_parser("bundle", help="check the .gcno bundle")
    bundle.add_argument("path", type=Path)
    bundle.add_argument(
        "--module", action="append", default=[], required=True,
        help="module directory that must contribute notes (repeatable)",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    if args.artifact == "summary":
        problems = check_summary(args.path, args.language)
    else:
        problems = check_bundle(args.path, args.module)
    for problem in problems:
        print(f"error: {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
