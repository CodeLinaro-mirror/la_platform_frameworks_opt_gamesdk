#!/usr/bin/env python3
"""Unit tests for agdk_coverage.gcov that need no toolchain."""

from pathlib import Path
import shutil
import tempfile
import unittest
import zipfile

from agdk_coverage import gcov

# 4-byte magic, 4-byte version, 4-byte stamp, as in real GCOV headers.
_NOTE_A = b"oncg*804AAAApayload"
_DATA_A = b"adcg*804AAAAcounts"
_DATA_B = b"adcg*804BBBBcounts"


class GcovHelpersTest(unittest.TestCase):

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="agdk_gcov_"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def test_read_stamp_and_find_notes(self):
        self.assertEqual(gcov.read_stamp(_NOTE_A), b"AAAA".hex())
        with self.assertRaises(ValueError):
            gcov.read_stamp(b"short")
        note_dir = self.tmp / ".cxx" / "x86_64" / "CMakeFiles" / "t.dir"
        note_dir.mkdir(parents=True)
        (note_dir / "Settings.cpp.gcno").write_bytes(_NOTE_A)
        (self.tmp / "missing").mkdir()
        notes = gcov.find_notes([self.tmp / ".cxx", self.tmp / "missing", self.tmp / "absent"])
        self.assertEqual(
            [(n.stem, n.stamp) for n in notes], [("Settings.cpp", b"AAAA".hex())]
        )

    def test_find_data_reads_directories_and_zips_and_deduplicates(self):
        pulled = self.tmp / "pulled"
        pulled.mkdir()
        (pulled / "Settings.cpp.gcda").write_bytes(_DATA_A)
        with zipfile.ZipFile(pulled / "files_1.zip", "w") as zf:
            zf.writestr("files/Settings.cpp.gcda", _DATA_A)  # same content, same key
            zf.writestr("files/Other.cpp.gcda", _DATA_B)
            zf.writestr("files/logcat.txt", b"ignored")
        data = gcov.find_data([pulled])
        self.assertEqual(
            {k: len(v) for k, v in data.items()},
            {("Settings.cpp", b"AAAA".hex()): 1, ("Other.cpp", b"BBBB".hex()): 1},
        )
        self.assertEqual(gcov.find_data([pulled / "files_1.zip"]), data)
        (pulled / "Broken.cpp.gcda").write_bytes(b"adcg")
        with self.assertRaises(ValueError):
            gcov.find_data([pulled])

    def test_parse_intermediate_accumulates_per_file_line_counts(self):
        text = (
            "file:/root/a.cpp\n"
            "function:3,1,_Z1av\n"
            "lcount:3,1\n"
            "lcount:4,0\n"
            "file:/root/a.h\n"
            "lcount:9,2,1\n"
            "lcount:9,3\n"
        )
        self.assertEqual(
            gcov.parse_intermediate(text),
            {"/root/a.cpp": {3: 1, 4: 0}, "/root/a.h": {9: 5}},
        )

    def test_run_llvm_cov_raises_on_tool_failure(self):
        fake = self.tmp / "llvm-cov"
        fake.write_text("#!/bin/sh\necho 'boom' >&2\nexit 3\n")
        fake.chmod(0o755)
        note = self.tmp / "a.cpp.gcno"
        note.write_bytes(_NOTE_A)
        with self.assertRaisesRegex(RuntimeError, r"llvm-cov gcov failed \(3\): boom"):
            gcov.run_llvm_cov(fake, [(note, None)])
        self.assertEqual(gcov.run_llvm_cov(fake, []), [])


if __name__ == "__main__":
    unittest.main()
