#!/usr/bin/env python3
"""Unit tests for check_coverage_artifacts.py."""

import io
import json
from pathlib import Path
import shutil
import tarfile
import tempfile
import unittest

import check_coverage_artifacts as check


def _write_summary(path: Path, files: dict[str, dict[str, object]]) -> Path:
    path.write_text(json.dumps({"files": files}), encoding="utf-8")
    return path


def _write_bundle(path: Path, members: list[str]) -> Path:
    with tarfile.open(path, "w:gz") as tar:
        for name in members:
            info = tarfile.TarInfo(name)
            info.size = 1
            tar.addfile(info, io.BytesIO(b"x"))
    return path


class CheckCoverageArtifactsTest(unittest.TestCase):

    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="agdk_check_"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def test_summary_passes_when_each_language_has_an_executed_line(self):
        summary = _write_summary(self.tmp / "s.json", {
            "a.cpp": {"language": "cpp", "executed_lines": 3},
            "b.cpp": {"language": "cpp", "executed_lines": 0},
            "A.java": {"language": "java", "executed_lines": 1},
        })
        self.assertEqual(check.check_summary(summary, ["cpp", "java"]), [])

    def test_summary_counts_executed_lines_from_schema_2_line_maps(self):
        summary = _write_summary(self.tmp / "s.json", {
            "a.cpp": {"language": "cpp", "lines": {"1": 0, "2": 0}},
            "A.java": {"language": "java", "lines": {"7": 2, "8": 0}},
        })
        self.assertEqual(
            check.check_summary(summary, ["cpp", "java"]),
            [f"{summary}: 1 cpp files, none with an executed line"],
        )

    def test_summary_reports_missing_and_unexecuted_languages(self):
        summary = _write_summary(self.tmp / "s.json", {
            "a.cpp": {"language": "cpp", "executed_lines": 0},
        })
        problems = check.check_summary(summary, ["cpp", "java"])
        self.assertEqual(len(problems), 2)
        self.assertIn("1 cpp files, none with an executed line", problems[0])
        self.assertIn("no java files", problems[1])

    def test_bundle_requires_notes_under_each_module(self):
        bundle = _write_bundle(self.tmp / "b.tar.gz", [
            "games-frame-pacing/.cxx/Debug/x/CMakeFiles/s.dir/a.cpp.gcno",
            "game-activity/.cxx/Debug/x/CMakeFiles/g.dir/b.cpp.gcda",
            "games-frame-pacing/common/a.cpp",
        ])
        self.assertEqual(
            check.check_bundle(bundle, ["games-frame-pacing"]), []
        )
        self.assertEqual(
            check.check_bundle(bundle, ["games-frame-pacing", "game-activity"]),
            [f"{bundle}: no .gcno notes under game-activity/.cxx/"],
        )

    def test_main_exit_code_reflects_problems(self):
        summary = _write_summary(self.tmp / "s.json", {
            "a.cpp": {"language": "cpp", "executed_lines": 2},
        })
        self.assertEqual(
            check.main(["summary", str(summary), "--language", "cpp"]), 0
        )
        self.assertEqual(
            check.main(["summary", str(summary), "--language", "java"]), 1
        )


if __name__ == "__main__":
    unittest.main()
