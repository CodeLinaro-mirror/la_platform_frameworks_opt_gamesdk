#!/usr/bin/env python3
"""Unit tests for agdk_coverage.paths."""

from pathlib import Path
import shutil
import tempfile
import unittest

from agdk_coverage import paths


class IsSdkSourceTest(unittest.TestCase):

    def test_module_and_shared_sources_are_sdk_code(self):
        for rel in (
            "games-frame-pacing/common/Settings.cpp",
            "src/common/system_utils.cpp",
            "include/swappy/swappy_common.h",
            "games-controller/src/main/cpp/GameController.cpp",
            "game-activity/src/main/java/com/google/androidgamesdk/GameActivity.java",
            "game-activity/prefab-src/modules/game-activity/src/game-activity/GameActivity.cpp",
        ):
            self.assertTrue(paths.is_sdk_source(rel), rel)

    def test_tests_samples_tooling_and_build_output_are_not(self):
        for rel in (
            "test/swappy/swappy_basic_test.cpp",
            "samples/bouncyball/app/src/main/cpp/Renderer.cpp",
            "third_party/json11/json11.cpp",
            "buildSrc/src/main/java/com/google/androidgamesdk/CMakeWrapper.kt",
            "tools/coverage/agdk_coverage/cli.py",
            "games-controller/src/androidTest/cpp/paddleboat_tests.cpp",
            "game-activity/src/test/java/com/google/androidgamesdk/GameActivityUnitTest.java",
            "games-frame-pacing/.cxx/Debug/x/CMakeFiles/a.cpp",
            "",
        ):
            self.assertFalse(paths.is_sdk_source(rel), rel)


class SourceMapperTest(unittest.TestCase):

    def setUp(self):
        self.repo = Path(tempfile.mkdtemp(prefix="agdk_paths_")) / "gamesdk"
        self.addCleanup(shutil.rmtree, self.repo.parent, ignore_errors=True)
        (self.repo / "src" / "common").mkdir(parents=True)
        (self.repo / "src" / "common" / "util.c").write_text("int x;\n")
        link_dir = self.repo / "game-activity" / "src" / "common"
        link_dir.mkdir(parents=True)
        (link_dir / "util.c").symlink_to("../../../src/common/util.c")
        (self.repo / "include").mkdir()
        (self.repo / "include" / "nanopb").symlink_to("../../external/nanopb")
        (self.repo.parent / "external" / "nanopb").mkdir(parents=True)
        (self.repo.parent / "external" / "nanopb" / "pb.h").write_text("")
        self.build_root = "/buildbot/src/gamesdk"
        self.mapper = paths.SourceMapper(self.build_root, self.repo)

    def test_resolves_symlinks_inside_the_repository(self):
        status, canonical = self.mapper(f"{self.build_root}/game-activity/src/common/util.c")
        self.assertEqual((status, canonical), (paths.OK, "src/common/util.c"))
        self.assertEqual(
            self.mapper(f"{self.build_root}/src/common/../common/util.c"),
            (paths.OK, "src/common/util.c"),
        )

    def test_classifies_external_excluded_and_unknown_paths(self):
        self.assertEqual(
            self.mapper("/prebuilts/ndk/sysroot/usr/include/jni.h"), (paths.EXTERNAL, None)
        )
        self.assertEqual(
            self.mapper(f"{self.build_root}/test/swappy/main.cpp"), (paths.EXCLUDED, None)
        )
        self.assertEqual(self.mapper("common/Settings.cpp"), (paths.UNKNOWN, None))
        # A symlink whose target leaves the repository cannot be attributed.
        self.assertEqual(
            self.mapper(f"{self.build_root}/include/nanopb/pb.h"), (paths.UNKNOWN, None)
        )

    def test_without_a_checkout_paths_are_only_relativized(self):
        mapper = paths.SourceMapper(self.build_root, None)
        self.assertEqual(
            mapper(f"{self.build_root}/game-activity/src/common/util.c"),
            (paths.OK, "game-activity/src/common/util.c"),
        )


class ModuleTableTest(unittest.TestCase):

    def test_native_build_dirs_cover_every_module(self):
        repo = Path("/r")
        self.assertEqual(
            paths.native_build_dirs(repo),
            [
                repo / "games-frame-pacing/.cxx",
                repo / "test/swappy/testapp/app/.cxx",
                repo / "games-controller/.cxx",
                repo / "game-activity/.cxx",
                repo / "game-text-input/.cxx",
            ],
        )


if __name__ == "__main__":
    unittest.main()
