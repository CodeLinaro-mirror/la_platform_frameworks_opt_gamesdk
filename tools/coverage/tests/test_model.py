#!/usr/bin/env python3
"""Unit tests for agdk_coverage.model."""

import unittest

from agdk_coverage import model


class CoverageSetTest(unittest.TestCase):

    def test_adding_a_file_twice_sums_counts_per_line(self):
        coverage = model.CoverageSet()
        coverage.add("src/common/util.c", model.CPP, {2: 4, 3: 0})
        coverage.add("src/common/util.c", model.CPP, {2: 1, 4: 2})
        self.assertEqual(coverage["src/common/util.c"].lines, {2: 5, 3: 0, 4: 2})
        self.assertEqual(len(coverage), 1)

    def test_ignores_files_without_executable_lines_and_rejects_conflicts(self):
        coverage = model.CoverageSet()
        coverage.add("Listener.java", model.JAVA, {})
        self.assertEqual(len(coverage), 0)
        with self.assertRaises(ValueError):
            coverage.add("", model.CPP, {1: 1})
        coverage.add("a.cpp", model.CPP, {1: 1})
        with self.assertRaises(ValueError):
            coverage.add("a.cpp", model.JAVA, {1: 1})

    def test_rows_are_sorted_and_filterable_by_language(self):
        coverage = model.CoverageSet()
        coverage.add("b/B.java", model.JAVA, {1: 1})
        coverage.add("a/a.cpp", model.CPP, {1: 0})
        self.assertEqual([r.path for r in coverage.rows()], ["a/a.cpp", "b/B.java"])
        self.assertEqual([r.path for r in coverage.rows(model.JAVA)], ["b/B.java"])


class TotalsTest(unittest.TestCase):

    def test_totals_and_incremental_coverage(self):
        coverage = model.CoverageSet()
        coverage.add("a.cpp", model.CPP, {1: 1, 2: 0, 3: 2})
        coverage.add("B.java", model.JAVA, {5: 0})
        annotated = coverage.with_changed_lines({"a.cpp": [2, 3, 4], "B.java": [9]})
        totals = model.totals(annotated.rows())
        self.assertEqual((totals.total_lines, totals.executed_lines), (4, 2))
        # Line 4 of a.cpp and line 9 of B.java are not executable.
        self.assertEqual((totals.diff_total_lines, totals.diff_executed_lines), (2, 1))
        self.assertAlmostEqual(totals.pct, 50.0)
        self.assertAlmostEqual(totals.diff_pct, 50.0)
        self.assertEqual(
            totals.as_dict(),
            {
                "total_lines": 4,
                "executed_lines": 2,
                "pct": 50.0,
                "diff_total_lines": 2,
                "diff_executed_lines": 1,
                "diff_pct": 50.0,
            },
        )

    def test_empty_totals_have_no_incremental_percentage(self):
        totals = model.totals([])
        self.assertEqual(totals.pct, 0.0)
        self.assertIsNone(totals.diff_pct)
        self.assertIsNone(totals.as_dict()["diff_pct"])


if __name__ == "__main__":
    unittest.main()
