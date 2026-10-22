#!/usr/bin/env python3
"""Unit tests for agdk_coverage.jacoco and agdk_coverage.gitdiff."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from agdk_coverage import gitdiff, jacoco

_JACOCO_XML = """<?xml version="1.0" encoding="UTF-8"?>
<report name="game-activity">
  <package name="com/google/androidgamesdk">
    <sourcefile name="GameActivity.java">
      <line nr="3" mi="0" ci="4" mb="0" cb="0"/>
      <line nr="4" mi="3" ci="0" mb="0" cb="0"/>
    </sourcefile>
    <sourcefile name="Missing.java">
      <line nr="1" mi="0" ci="1" mb="0" cb="0"/>
    </sourcefile>
  </package>
  <package name="com/google/androidgamesdk/gametextinput">
    <sourcefile name="Listener.java">
      <line nr="8" mi="0" ci="2" mb="0" cb="0"/>
    </sourcefile>
  </package>
</report>
"""


class JacocoTest(unittest.TestCase):

    def test_sources_are_located_under_module_roots_and_symlinks_resolved(self):
        tmp = Path(tempfile.mkdtemp(prefix="agdk_jacoco_"))
        self.addCleanup(shutil.rmtree, tmp, ignore_errors=True)
        repo = tmp / "gamesdk"
        ga_pkg = repo / "game-activity/src/main/java/com/google/androidgamesdk"
        gti_pkg = repo / "game-text-input/src/main/java/com/google/androidgamesdk/gametextinput"
        ga_pkg.mkdir(parents=True)
        gti_pkg.mkdir(parents=True)
        (ga_pkg / "GameActivity.java").write_text("class GameActivity {}\n")
        (gti_pkg / "Listener.java").write_text("interface Listener {}\n")
        # game-activity ships the gametextinput package as a symlink.
        (ga_pkg / "gametextinput").symlink_to(
            "../../../../../../../game-text-input/src/main/java/com/google/androidgamesdk/gametextinput"
        )
        xml_path = tmp / "jacocoTestReport.xml"
        xml_path.write_text(_JACOCO_XML, encoding="utf-8")

        files, unknown = jacoco.parse(
            [xml_path],
            repo,
            [repo / "game-activity/src/main/java", repo / "game-text-input/src/main/java"],
        )
        self.assertEqual(
            files,
            {
                "game-activity/src/main/java/com/google/androidgamesdk/GameActivity.java": {3: 4, 4: 0},
                "game-text-input/src/main/java/com/google/androidgamesdk/gametextinput/Listener.java": {8: 2},
            },
        )
        self.assertEqual(unknown, ["com/google/androidgamesdk/Missing.java"])


class GitDiffTest(unittest.TestCase):

    def test_parse_unified_diff_extracts_added_and_modified_lines(self):
        diff_text = (
            "diff --git a/games-frame-pacing/common/SwappyCommon.cpp"
            " b/games-frame-pacing/common/SwappyCommon.cpp\n"
            "--- a/games-frame-pacing/common/SwappyCommon.cpp\n"
            "+++ b/games-frame-pacing/common/SwappyCommon.cpp\n"
            "@@ -10,0 +11,3 @@ void foo() {\n"
            "+  int a = 1;\n+  int b = 2;\n+  int c = 3;\n"
            "@@ -25,2 +28 @@ void bar() {\n"
            "-  old1();\n-  old2();\n+  new1();\n"
            "@@ -40,3 +42,0 @@ void deleted_only() {\n"
            "-  gone();\n"
            "diff --git a/gone.cpp b/gone.cpp\n--- a/gone.cpp\n+++ /dev/null\n"
            "@@ -1,2 +0,0 @@\n-x\n-y\n"
        )
        self.assertEqual(
            gitdiff.parse_unified_diff(diff_text),
            {"games-frame-pacing/common/SwappyCommon.cpp": {11, 12, 13, 28}},
        )

    def test_changed_lines_against_a_real_commit(self):
        repo = Path(tempfile.mkdtemp(prefix="agdk_git_"))
        self.addCleanup(shutil.rmtree, repo, ignore_errors=True)

        def git(*args: str) -> None:
            subprocess.run(
                ["git", "-c", "user.name=t", "-c", "user.email=t@t", "-C", str(repo), *args],
                check=True,
                capture_output=True,
                timeout=60,
            )

        git("init", "-q")
        (repo / "a.cpp").write_text("one\ntwo\n")
        git("add", "a.cpp")
        git("commit", "-q", "-m", "base")
        (repo / "a.cpp").write_text("one\nTWO\nthree\n")
        git("commit", "-q", "-am", "change")
        self.assertEqual(gitdiff.changed_lines(repo, "HEAD~1"), {"a.cpp": {2, 3}})
        with self.assertRaises(ValueError):
            gitdiff.changed_lines(repo, "HEAD~5")


if __name__ == "__main__":
    unittest.main()
