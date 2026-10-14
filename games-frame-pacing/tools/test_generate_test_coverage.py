#!/usr/bin/env python3
"""Unit tests for generate_test_coverage.py."""

from pathlib import Path
import shutil
import tarfile
import tempfile
import unittest
import zipfile

import generate_test_coverage as cov


class GenerateTestCoverageTest(unittest.TestCase):

    def test_read_gcov_stamp_extracts_bytes_8_to_12(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            gcno = Path(tmp) / "Sample.cpp.gcno"
            gcno.write_bytes(b"oncg*504STAMP_REST")
            self.assertEqual(cov._read_gcov_stamp(gcno), b"STAM")

            short_file = Path(tmp) / "Short.cpp.gcno"
            short_file.write_bytes(b"short")
            self.assertIsNone(cov._read_gcov_stamp(short_file))

    def test_sync_pulled_gcda_matches_by_header_stamp_and_rejects_mismatch(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            arm_dir = root / ".cxx" / "Debug" / "arm64-v8a"
            x86_dir = root / ".cxx" / "Debug" / "x86_64"
            pull_dir = root / "pulled"
            arm_dir.mkdir(parents=True)
            x86_dir.mkdir(parents=True)
            pull_dir.mkdir(parents=True)

            (arm_dir / "SwappyCommon.cpp.gcno").write_bytes(b"oncg*504ARM8xxxx")
            (x86_dir / "SwappyCommon.cpp.gcno").write_bytes(b"oncg*504X86_xxxx")

            pulled_gcda = pull_dir / "SwappyCommon.cpp.gcda"
            pulled_gcda.write_bytes(b"adcg*504ARM8payload")

            synced = cov._sync_pulled_gcda_files(
                [pulled_gcda], [root / ".cxx"]
            )
            self.assertEqual(synced, 1)
            self.assertEqual(
                (arm_dir / "SwappyCommon.cpp.gcda").read_bytes(),
                b"adcg*504ARM8payload",
            )
            self.assertFalse((x86_dir / "SwappyCommon.cpp.gcda").exists())

            mismatched_gcda = pull_dir / "other" / "SwappyCommon.cpp.gcda"
            mismatched_gcda.parent.mkdir(parents=True)
            mismatched_gcda.write_bytes(b"adcg*504NONEpayload")
            self.assertEqual(
                cov._sync_pulled_gcda_files([mismatched_gcda], [root / ".cxx"]),
                0,
            )
            self.assertEqual(
                (arm_dir / "SwappyCommon.cpp.gcda").read_bytes(),
                b"adcg*504ARM8payload",
            )

    def test_find_active_gcno_files_includes_unexecuted_same_abi_and_host_notes(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            arm_dir = root / ".cxx" / "Debug" / "arm64-v8a"
            x86_dir = root / ".cxx" / "Debug" / "x86_64"
            arm_dir.mkdir(parents=True)
            x86_dir.mkdir(parents=True)

            (arm_dir / "Executed.cpp.gcno").write_bytes(b"oncg*504ARM8")
            (arm_dir / "Executed.cpp.gcda").write_bytes(b"adcg*504ARM8")
            (arm_dir / "Unexecuted.cpp.gcno").write_bytes(b"oncg*504ARM8")
            (x86_dir / "Unexecuted.cpp.gcno").write_bytes(b"oncg*504X86_")

            active = cov._find_active_gcno_files([root / ".cxx"])
            self.assertEqual(
                set(active),
                {
                    arm_dir / "Executed.cpp.gcno",
                    arm_dir / "Unexecuted.cpp.gcno",
                },
            )

    def test_package_gcno_bundle_and_offline_report_without_gamesdk_dir(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            gamesdk_dir = Path(tmp) / "gamesdk"
            module_dir = gamesdk_dir / "games-frame-pacing"
            tools_dir = module_dir / "tools"
            common_dir = module_dir / "common"
            gcno_dir = module_dir / ".cxx" / "Debug" / "arm64-v8a"
            tools_dir.mkdir(parents=True)
            common_dir.mkdir(parents=True)
            gcno_dir.mkdir(parents=True)

            src_file = common_dir / "Settings.cpp"
            src_file.write_text(
                "int hit() {\n  return 1;\n}\n", encoding="utf-8"
            )
            real_template = (
                Path(__file__).resolve().parent / "coverage_report_template.html"
            )
            (tools_dir / "coverage_report_template.html").write_text(
                real_template.read_text(encoding="utf-8"), encoding="utf-8"
            )
            (tools_dir / "generate_test_coverage.py").write_text(
                "# stub\n", encoding="utf-8"
            )
            (gcno_dir / "Settings.cpp.gcno").write_bytes(b"oncg*504ARM8xxxx")

            bundle_path = Path(tmp) / "dist" / "swappy_coverage_bundle.tar.gz"
            cov.package_gcno_bundle(gamesdk_dir, module_dir, bundle_path)
            with tarfile.open(bundle_path, "r:gz") as tar:
                self.assertEqual(
                    sorted(tar.getnames()),
                    [
                        "games-frame-pacing/.cxx/Debug/arm64-v8a/Settings.cpp.gcno",
                        "games-frame-pacing/common/Settings.cpp",
                        "games-frame-pacing/tools/coverage_report_template.html",
                        "games-frame-pacing/tools/generate_test_coverage.py",
                    ],
                )

            # Simulate a clean CI machine where the compile-time gamesdk_dir is absent.
            shutil.rmtree(gamesdk_dir)

            gcda_dir = Path(tmp) / "pulled_gcda"
            gcda_dir.mkdir()
            with zipfile.ZipFile(gcda_dir / "files_123.zip", "w") as zf:
                zf.writestr("Settings.cpp.gcda", b"adcg*504ARM8data")

            fake_llvm_cov = Path(tmp) / "fake_llvm_cov.py"
            fake_llvm_cov.write_text(
                "#!/usr/bin/env python3\n"
                "from pathlib import Path\n"
                "import sys\n"
                "gcno = Path(sys.argv[-1])\n"
                "gcda = gcno.with_suffix('.gcda')\n"
                "hits = 5 if gcda.exists() else 0\n"
                f"Path('Settings.cpp.gcov').write_text('file:{src_file}\\nlcount:1,' + str(hits) + '\\nlcount:2,0\\n')\n",
                encoding="utf-8",
            )
            fake_llvm_cov.chmod(0o755)

            out_dir = Path(tmp) / "ci_report_out"
            report_html = cov.run_report_from_artifacts(
                gamesdk_dir=gamesdk_dir,
                module_dir=module_dir,
                tools_dir=tools_dir,
                llvm_cov_bin=fake_llvm_cov,
                gcda_dir=gcda_dir,
                bundle_path=bundle_path,
                output_dir=out_dir,
            )
            self.assertTrue(report_html.exists())
            self.assertIn("50.0%", report_html.read_text(encoding="utf-8"))
            detail_html = out_dir / "sources" / "common__Settings.cpp.html"
            self.assertIn("5x", detail_html.read_text(encoding="utf-8"))
            self.assertIn("int hit()", detail_html.read_text(encoding="utf-8"))

    def test_parse_gcov_file_accumulates_line_counts(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            module_dir = Path(tmp)
            common_dir = module_dir / "common"
            common_dir.mkdir()
            src_file = common_dir / "Foo.cpp"
            src_file.write_text("int a = 1;\nint b = 2;\n", encoding="utf-8")

            target_map = {"foo.cpp": "common/Foo.cpp"}
            executed: dict[str, dict[int, int]] = {"common/Foo.cpp": {}}

            for counts in ((4, "#####"), (3, 2)):
                gcov_file = module_dir / "Foo.cpp.gcov"
                gcov_file.write_text(
                    f"        -:    0:Source:{src_file}\n"
                    f"        {counts[0]}:    1:int a = 1;\n"
                    f"    {counts[1]}:    2:int b = 2;\n",
                    encoding="utf-8",
                )
                cov._parse_gcov_file(
                    gcov_file, module_dir, target_map, executed
                )
            self.assertEqual(executed["common/Foo.cpp"], {1: 7, 2: 2})

    def test_generate_html_report_renders_clickable_source_views(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            gamesdk_dir = Path(tmp) / "gamesdk"
            module_dir = gamesdk_dir / "games-frame-pacing"
            tools_dir = module_dir / "tools"
            common_dir = module_dir / "common"
            tools_dir.mkdir(parents=True)
            common_dir.mkdir(parents=True)

            real_template = (
                Path(__file__).resolve().parent / "coverage_report_template.html"
            )
            (tools_dir / "coverage_report_template.html").write_text(
                real_template.read_text(encoding="utf-8"), encoding="utf-8"
            )
            (common_dir / "Settings.cpp").write_text(
                "void hit() {\n  int x = 1;\n}\n", encoding="utf-8"
            )

            rows = [
                cov.CoverageSummary(
                    relative_path="common/Settings.cpp",
                    total_lines=2,
                    executed_lines=1,
                    line_counts={1: 5, 2: 0},
                )
            ]
            out_dir = Path(tmp) / "report_out"
            index_html = cov.generate_html_report(
                tools_dir, gamesdk_dir, rows, output_dir=out_dir
            )
            index_text = index_html.read_text(encoding="utf-8")
            self.assertIn(
                'href="sources/common__Settings.cpp.html"', index_text
            )
            detail_html = out_dir / "sources" / "common__Settings.cpp.html"
            self.assertTrue(detail_html.exists())
            detail_text = detail_html.read_text(encoding="utf-8")
            self.assertIn("void hit()", detail_text)
            self.assertIn("5x", detail_text)
            self.assertIn("line-covered", detail_text)
            self.assertIn("line-missed", detail_text)


if __name__ == "__main__":
    unittest.main()
