#!/usr/bin/env python3
"""Unit tests for agdk_coverage.cli that need no toolchain."""

import io
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock

from agdk_coverage import cli


class FindLlvmCovTest(unittest.TestCase):

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="agdk_cli_"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.repo = self.tmp / "checkout" / "gamesdk"
        self.repo.mkdir(parents=True)

    def test_explicit_path_wins_and_must_exist(self):
        explicit = self.tmp / "llvm-cov"
        explicit.write_text("")
        self.assertEqual(cli.find_llvm_cov(explicit, self.repo), explicit)
        with self.assertRaises(FileNotFoundError):
            cli.find_llvm_cov(self.tmp / "missing", self.repo)

    def test_path_then_ndk_prebuilts_then_error(self):
        with mock.patch.dict(os.environ, {"PATH": str(self.tmp / "empty")}):
            with self.assertRaises(FileNotFoundError):
                cli.find_llvm_cov(None, self.repo)
            for ndk in ("r26", "r27"):
                binary = (
                    self.repo.parent / "prebuilts" / "ndk" / ndk / "toolchains" / "llvm"
                    / "prebuilt" / "linux-x86_64" / "bin" / "llvm-cov"
                )
                binary.parent.mkdir(parents=True)
                binary.write_text("")
            self.assertEqual(cli.find_llvm_cov(None, self.repo).parts[-7], "r27")
            on_path = self.tmp / "bin"
            on_path.mkdir()
            (on_path / "llvm-cov").write_text("#!/bin/sh\n")
            (on_path / "llvm-cov").chmod(0o755)
            with mock.patch.dict(os.environ, {"PATH": str(on_path)}):
                self.assertEqual(cli.find_llvm_cov(None, self.repo), on_path / "llvm-cov")


class ParseArgsTest(unittest.TestCase):

    def test_report_and_bundle_subcommands(self):
        args = cli.parse_args(["report", "--out", "o", "--gcda-dir", "a.zip", "--gcda-dir", "b"])
        self.assertEqual(args.command, "report")
        self.assertEqual(args.gcda_dirs, [Path("a.zip"), Path("b")])
        self.assertEqual(args.native_build_dirs, [])
        args = cli.parse_args(["bundle", "--out", "b.tar.gz", "--with-sources"])
        self.assertTrue(args.with_sources)
        with self.assertRaises(SystemExit):
            cli.parse_args(["report"])

    def test_changed_lines_warns_and_returns_none_when_diff_base_is_missing(self):
        tmp = Path(tempfile.mkdtemp(prefix="agdk_diff_"))
        self.addCleanup(shutil.rmtree, tmp, ignore_errors=True)
        stderr = io.StringIO()
        with mock.patch("sys.stderr", stderr):
            self.assertIsNone(cli._changed_lines(tmp, "HEAD~1"))
        self.assertIn("warning: incremental coverage disabled", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
