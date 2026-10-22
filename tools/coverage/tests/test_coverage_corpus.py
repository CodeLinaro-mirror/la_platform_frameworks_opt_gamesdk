#!/usr/bin/env python3
"""End-to-end regression test of the coverage pipeline on the GCOV corpus.

The corpus under fixtures/corpus/ was compiled once by build_corpus.sh with the
NDK host clang and mirrors the structural cases of the real repository: one
translation unit compiled into two CMake targets, one shared source reached
by absolute path and by a prefab-src symlink, a header reached through a
symlinked include directory, a header outside the repository root, test
sources under test/ and androidTest/, and .gcda files both next to their
notes (host runs) and flattened into a pulled zip archive (device runs).

The test runs the real `llvm-cov gcov` on the corpus and compares the
per-file line counts with the pinned expectations, so changes to pairing,
path mapping, or exclusion policy show up as a reviewed fixture diff.
"""

import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest

from agdk_coverage import bundle, cli, gcov, paths

CORPUS_DIR = Path(__file__).resolve().parent / "fixtures" / "corpus"
# Set by build.sh so CI fails instead of skipping when llvm-cov is missing.
REQUIRE_LLVM_COV_ENV = "AGDK_COVERAGE_REQUIRE_LLVM_COV"
with (CORPUS_DIR / "metadata.json").open(encoding="utf-8") as _metadata:
    BUILD_ROOT: str = json.load(_metadata)["build_root"]


def _find_llvm_cov() -> Path:
    # tests/ -> coverage/ -> tools/ -> gamesdk/.
    gamesdk_dir = Path(__file__).resolve().parents[3]
    try:
        return cli.find_llvm_cov(None, gamesdk_dir)
    except FileNotFoundError as err:
        if os.environ.get(REQUIRE_LLVM_COV_ENV):
            raise
        raise unittest.SkipTest(f"{err} (set {REQUIRE_LLVM_COV_ENV}=1 to fail)")


def _load_expected(name: str) -> dict[str, dict[str, object]]:
    with (CORPUS_DIR / name).open(encoding="utf-8") as f:
        return json.load(f)


class CoverageCorpusTest(unittest.TestCase):

    def setUp(self) -> None:
        self.llvm_cov = _find_llvm_cov()
        self.tmp = Path(tempfile.mkdtemp(prefix="agdk_corpus_"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.gamesdk_dir = self.tmp / "gamesdk"
        shutil.copytree(CORPUS_DIR / "tree" / "gamesdk", self.gamesdk_dir,
                        symlinks=True)
        shutil.copytree(CORPUS_DIR / "notes", self.gamesdk_dir,
                        dirs_exist_ok=True)
        self.cxx_dirs = [
            self.gamesdk_dir / "games-frame-pacing" / ".cxx",
            self.gamesdk_dir / "game-activity" / ".cxx",
            self.gamesdk_dir / "test" / "swappy" / "testapp" / "app" / ".cxx",
        ]

    def _v2_report(self, out_dir: Path, extra_args: list[str]) -> dict[str, object]:
        args = [
            "report",
            "--llvm-cov", str(self.llvm_cov),
            "--repo", str(self.gamesdk_dir),
            "--out", str(out_dir),
            *extra_args,
        ]
        self.assertEqual(cli.main(args), 0)
        with (out_dir / "coverage_summary.json").open(encoding="utf-8") as f:
            return json.load(f)

    def test_v2_report_matches_pinned_line_counts(self) -> None:
        summary = self._v2_report(self.tmp / "v2", [
            "--build-root", BUILD_ROOT,
            *(a for d in self.cxx_dirs for a in ("--native-build-dir", str(d))),
            "--gcda-dir", str(CORPUS_DIR / "pulled"),
        ])
        actual = {
            path: info["lines"] for path, info in summary["files"].items()
        }
        self.assertEqual(actual, _load_expected("expected_v2.json"))
        self.assertEqual(
            summary["gcov"],
            {
                "notes": 8,
                "units": 4,
                "gcda_files": 7,
                "paired_gcda": 5,
                "unmatched_gcda": 0,
                "external_files": 1,
                "excluded_files": 2,
                "unknown_files": [],
            },
        )

    def test_v2_bundle_roundtrip_matches_the_tree_report(self) -> None:
        bundle_path = self.tmp / "agdk_coverage_bundle.tar.gz"
        # The corpus was compiled under BUILD_ROOT, so map it onto the copy.
        manifest = bundle.write(
            bundle_path,
            self.gamesdk_dir,
            gcov.find_notes(self.cxx_dirs),
            self.llvm_cov,
            paths.SourceMapper(BUILD_ROOT, self.gamesdk_dir),
            with_sources=True,
            diff_base="HEAD~1",
            changed_lines={"src/common/util.c": {3, 4}},
        )
        self.assertEqual(len(manifest["notes"]), 8)
        self.assertEqual(manifest["stats"]["unknown_files"], [])
        self.assertEqual(manifest["changed_lines"], {"src/common/util.c": [3, 4]})
        # The bundle carries notes only; host-run and device .gcda files are
        # passed in like CI artifacts.
        from_bundle = self._v2_report(self.tmp / "from_bundle", [
            "--bundle", str(bundle_path),
            *(a for d in self.cxx_dirs for a in ("--gcda-dir", str(d))),
            "--gcda-dir", str(CORPUS_DIR / "pulled"),
        ])
        self.assertEqual(
            {p: i["lines"] for p, i in from_bundle["files"].items()},
            _load_expected("expected_v2.json"),
        )
        self.assertEqual(from_bundle["diff_base"], "HEAD~1")
        self.assertEqual(
            from_bundle["files"]["src/common/util.c"]["changed_lines"], [3, 4]
        )
        self.assertEqual(
            from_bundle["gcov"],
            {
                "notes": 8,
                "units": 4,
                "gcda_files": 7,
                "paired_gcda": 5,
                "unmatched_gcda": 0,
                "external_files": 1,
                "excluded_files": 2,
                "unknown_files": [],
            },
        )
        page = self.tmp / "from_bundle" / "sources" / "src__common__util.c.html"
        self.assertIn("util_add", page.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
