#!/usr/bin/env python3
"""Unit tests for agdk_coverage.bundle."""

import io
import json
from pathlib import Path
import shutil
import tarfile
import tempfile
import unittest

from agdk_coverage import bundle, paths


class BundleTest(unittest.TestCase):

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="agdk_bundle_"))
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def test_manifest_mapper_treats_unlisted_paths_as_external(self):
        mapper = bundle.ManifestMapper({"/b/gamesdk/src/common/a.cpp": "src/common/a.cpp"})
        self.assertEqual(mapper("/b/gamesdk/src/common/../common/a.cpp"), (paths.OK, "src/common/a.cpp"))
        self.assertEqual(mapper("/sysroot/jni.h"), (paths.EXTERNAL, None))

    def test_read_rejects_bundles_without_a_v2_manifest(self):
        legacy = self.tmp / "legacy.tar.gz"
        with tarfile.open(legacy, "w:gz") as tar:
            info = tarfile.TarInfo("games-frame-pacing/.cxx/a.gcno")
            info.size = 1
            tar.addfile(info, io.BytesIO(b"x"))
        with self.assertRaisesRegex(ValueError, "not a v2 bundle"):
            bundle.read(legacy, self.tmp / "out_legacy")

        wrong = self.tmp / "wrong.tar.gz"
        payload = json.dumps({"schema_version": 7}).encode("utf-8")
        with tarfile.open(wrong, "w:gz") as tar:
            info = tarfile.TarInfo(bundle.MANIFEST_MEMBER)
            info.size = len(payload)
            tar.addfile(info, io.BytesIO(payload))
        with self.assertRaisesRegex(ValueError, "unsupported schema_version 7"):
            bundle.read(wrong, self.tmp / "out_wrong")


if __name__ == "__main__":
    unittest.main()
