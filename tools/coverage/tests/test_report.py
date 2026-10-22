#!/usr/bin/env python3
"""Unit tests for agdk_coverage.report."""

import json
from pathlib import Path
import shutil
import tempfile
import unittest

from agdk_coverage import model, report


def _coverage() -> model.CoverageSet:
    coverage = model.CoverageSet()
    coverage.add("games-frame-pacing/common/Settings.cpp", model.CPP, {2: 4, 3: 0})
    coverage.add("game-activity/src/main/java/com/google/androidgamesdk/GameActivity.java", model.JAVA, {3: 1})
    return coverage.with_changed_lines({"games-frame-pacing/common/Settings.cpp": [2, 3, 9]})


class ReportTest(unittest.TestCase):

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="agdk_report_"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def test_summary_json_has_schema_2_shape(self):
        path = report.write_summary_json(self.tmp, _coverage(), "HEAD~1", {"notes": 2})
        payload = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(payload["schema_version"], 2)
        self.assertEqual(payload["diff_base"], "HEAD~1")
        self.assertEqual(payload["gcov"], {"notes": 2})
        self.assertNotIn("module", payload)
        self.assertEqual(payload["cpp"]["diff_total_lines"], 2)
        self.assertEqual(payload["cpp"]["diff_executed_lines"], 1)
        self.assertEqual(payload["java"]["total_lines"], 1)
        self.assertEqual(payload["overall"]["pct"], 66.67)
        self.assertEqual(
            payload["files"]["games-frame-pacing/common/Settings.cpp"],
            {"language": "cpp", "lines": {"2": 4, "3": 0}, "changed_lines": [2, 3, 9]},
        )

    def test_html_index_links_to_source_pages_with_line_annotations(self):
        sources = {"games-frame-pacing/common/Settings.cpp": "void a() {\n  int x = 1;\n  int y = 2;\n}\n"}
        index = report.write_html(self.tmp, _coverage(), sources.get)
        text = index.read_text(encoding="utf-8")
        self.assertIn('href="sources/games-frame-pacing__common__Settings.cpp.html"', text)
        self.assertIn('data-changed="true" data-lang="cpp"', text)
        self.assertIn("<title>AGDK Code Coverage Report</title>", text)
        page = (self.tmp / "sources" / "games-frame-pacing__common__Settings.cpp.html").read_text(encoding="utf-8")
        self.assertIn("4x", page)
        self.assertIn("line-missed line-changed", page)
        self.assertIn("int x = 1;", page)
        missing = (self.tmp / "sources" / report.detail_filename(
            "game-activity/src/main/java/com/google/androidgamesdk/GameActivity.java"
        ).removeprefix("sources/")).read_text(encoding="utf-8")
        self.assertIn("Source file not found", missing)


if __name__ == "__main__":
    unittest.main()
