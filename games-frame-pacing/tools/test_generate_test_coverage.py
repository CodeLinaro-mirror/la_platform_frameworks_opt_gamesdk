#!/usr/bin/env python3
"""Unit tests for generate_test_coverage.py."""

from pathlib import Path
import tempfile
import unittest

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


if __name__ == "__main__":
    unittest.main()
