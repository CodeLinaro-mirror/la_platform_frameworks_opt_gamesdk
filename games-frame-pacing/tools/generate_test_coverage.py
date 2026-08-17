#!/usr/bin/env python3
"""Automated C++ test coverage generator for games-frame-pacing (Swappy).

Usage:
        python3 games-frame-pacing/tools/generate_test_coverage.py
"""

from dataclasses import dataclass
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from typing import Dict, List, Optional, Tuple, Type
from contextlib import ExitStack

MODULE_NAME = "games-frame-pacing"
TEST_PKG = "com.swappy.testapp.test"
APP_PKG = "com.swappy.testapp"


@dataclass(frozen=True)
class CoverageSummary:
    """Summary row of line coverage for a single source file."""
    relative_path: str
    total_lines: int
    executed_lines: int

    @property
    def percentage(self) -> float:
        """Calculates executed line coverage percentage."""
        if self.total_lines > 0:
            return self.executed_lines / self.total_lines * 100.0
        return 0.0


class ManagedFileBackup:
    """Context manager for temporary file modifications.

    Backs up a file on entry and restores original byte content on exit (whether
    due to normal exit or an exception), ensuring stateless build operations.

    Attributes:
        filepath: Path to the target file.
        exists: Whether the file existed prior to entering context.
        original_content: Raw byte content, or None if non-existent.
    """

    def __init__(self, filepath: Path) -> None:
        self.filepath = filepath
        self.exists = filepath.exists()
        self.original_content: Optional[bytes] = (
                filepath.read_bytes() if self.exists else None
        )

    def __enter__(self) -> "ManagedFileBackup":
        return self

    def __exit__(
            self,
            exc_type: Optional[Type[BaseException]],
            exc_val: Optional[BaseException],
            exc_tb: Optional[object],
    ) -> None:
        if self.exists and self.original_content is not None:
            self.filepath.write_bytes(self.original_content)
        elif not self.exists and self.filepath.exists():
            self.filepath.unlink()


def _configure_root_gradle(
        gamesdk_dir: Path, ndk_prebuilt: Path, stack: ExitStack
) -> None:
    """Injects ndkPath into root build.gradle if needed."""
    root_gradle = gamesdk_dir / "build.gradle"
    if root_gradle.exists():
        stack.enter_context(ManagedFileBackup(root_gradle))
        content = root_gradle.read_text(encoding="utf-8")
        if ndk_prebuilt.exists() and "ndkPath" not in content:
            injection = (
                    '\n    plugins.withId("com.android.library") {\n'
                    "        android {\n"
                    '            ndkPath'
                    f' file("{ndk_prebuilt.resolve()}").absolutePath\n'
                    "        }\n"
                    "    }\n"
            )
            content = re.sub(
                    r"(allprojects\s*\{[^\}]*repositories\s*\{[^\}]*\})",
                    r"\1" + injection,
                    content,
                    count=1,
            )
            root_gradle.write_text(content, encoding="utf-8")


def _configure_module_gradle_files(
        module_dir: Path, ndk_prebuilt: Path, stack: ExitStack
) -> None:
    """Injects ndkPath and gcov flags into module Gradle files."""
    build_gradle = module_dir / "build.gradle"
    extras_gradle = module_dir / "extras" / "build.gradle"

    for bg in [build_gradle, extras_gradle]:
        if bg.exists():
            stack.enter_context(ManagedFileBackup(bg))
            content = bg.read_text(encoding="utf-8")
            if "ndkPath" not in content and ndk_prebuilt.exists():
                content = content.replace(
                        "android {\n",
                        f'android {{\n    ndkPath "{ndk_prebuilt.resolve()}"\n',
                        1,
                )
            has_cov = "DCMAKE_CXX_FLAGS=--coverage" in content
            if bg == build_gradle and not has_cov:
                coverage_flags = (
                        'cmakeArgs << "-DCMAKE_CXX_FLAGS=--coverage"\n'
                        '                    cmakeArgs << "-DCMAKE_SHARED_LINKER_FLAGS=--coverage"'
                )
                if 'cmakeArgs << ("-DBUILD_TEST=ON")' in content:
                    content = content.replace(
                        'cmakeArgs << ("-DBUILD_TEST=ON")',
                        (
                            'cmakeArgs << ("-DBUILD_TEST=ON")\n'
                            f"                    {coverage_flags}"
                        ),
                    )
                elif "def cmakeArgs = []" in content:
                    content = content.replace(
                            "def cmakeArgs = []",
                            "def cmakeArgs = []\n"
                            f"                {coverage_flags}",
                    )
            bg.write_text(content, encoding="utf-8")


def _inject_swappy_flush_hook(module_dir: Path, stack: ExitStack) -> None:
    """Injects runtime gcov coverage flush hook into SwappyCommon.cpp."""
    swappy_common_cpp = module_dir / "common" / "SwappyCommon.cpp"
    if swappy_common_cpp.exists():
        stack.enter_context(ManagedFileBackup(swappy_common_cpp))
        flush_hook = (
                "\nextern \"C\" void __gcov_flush(void)"
                " __attribute__((weak));\n"
                "extern \"C\" __attribute__((visibility(\"default\"))) void"
                " Swappy_flushCoverage() {\n"
                "    if (__gcov_flush) {\n"
                "        __gcov_flush();\n"
                "    }\n"
                "}\n"
        )
        cpp_text = swappy_common_cpp.read_text(encoding="utf-8")
        if "Swappy_flushCoverage" not in cpp_text:
            swappy_common_cpp.write_text(
                cpp_text + flush_hook, encoding="utf-8"
            )


def _configure_testapp_files(
        gamesdk_dir: Path, ndk_prebuilt: Path, stack: ExitStack
) -> None:
    """Configures testapp build.gradle, CMakeLists.txt, and local.properties."""
    repo_root = gamesdk_dir.parent
    testapp_dir = gamesdk_dir / "test" / "swappy" / "testapp"
    testapp_local_props = testapp_dir / "local.properties"
    stack.enter_context(ManagedFileBackup(testapp_local_props))
    sdk_dir = repo_root / "prebuilts" / "sdk"
    cmake_dir = repo_root / "prebuilts" / "cmake" / ("darwin-x86" if sys.platform == "darwin" else "linux-x86")
    props = (
            f"sdk.dir={sdk_dir.resolve()}\n"
            f"android.ndkPath={ndk_prebuilt.resolve()}\n"
            f"cmake.dir={cmake_dir.resolve()}\n"
    )
    testapp_local_props.write_text(props, encoding="utf-8")

    testapp_build_gradle = testapp_dir / "app" / "build.gradle"
    if testapp_build_gradle.exists():
        stack.enter_context(ManagedFileBackup(testapp_build_gradle))
        t_content = testapp_build_gradle.read_text(encoding="utf-8")
        if "ndkPath" not in t_content:
            t_content = t_content.replace(
                    "android {\n",
                    f'android {{\n    ndkPath "{ndk_prebuilt.resolve()}"\n',
                    1,
            )
        t_content = t_content.replace("compileSdk 35", "compileSdk 31")
        t_content = t_content.replace("targetSdk 35", "targetSdk 31")
        testapp_build_gradle.write_text(t_content, encoding="utf-8")

    testapp_cmake = (
        testapp_dir / "app" / "src" / "main" / "cpp" / "CMakeLists.txt"
    )
    if testapp_cmake.exists():
        stack.enter_context(ManagedFileBackup(testapp_cmake))
        c_text = testapp_cmake.read_text(encoding="utf-8")
        if "--coverage" not in c_text:
            c_flags = (
                    'set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -O0 -g'
                    ' -Wno-error" )\n'
                    'set( CMAKE_SHARED_LINKER_FLAGS'
                    ' "${CMAKE_SHARED_LINKER_FLAGS}'
                    ' -Wl,-z,max-page-size=16384 --coverage" )\n'
            )
            c_text = c_text.replace(
                    'set( CMAKE_SHARED_LINKER_FLAGS'
                    ' "${CMAKE_SHARED_LINKER_FLAGS}'
                    ' -Wl,-z,max-page-size=16384" )',
                    c_flags,
            )
            testapp_cmake.write_text(c_text, encoding="utf-8")


def _inject_gcov_flush_environment(gamesdk_dir: Path, stack: ExitStack) -> None:
    """Registers global GcovFlushEnvironment GTest hook."""
    swappy_test_cpp = gamesdk_dir / "test" / "swappy" / "swappycommon_test.cpp"
    if swappy_test_cpp.exists():
        stack.enter_context(ManagedFileBackup(swappy_test_cpp))
        st_text = swappy_test_cpp.read_text(encoding="utf-8")
        if "GcovFlushEnvironment" not in st_text:
            env_hook = (
                    "\nextern \"C\" void Swappy_flushCoverage(void)"
                    " __attribute__((weak));\n"
                    "extern \"C\" void __gcov_dump(void)"
                    " __attribute__((weak));\n"
                    "extern \"C\" void __gcov_flush(void)"
                    " __attribute__((weak));\n"
                    "\nclass GcovFlushEnvironment : public"
                    " ::testing::Environment {\n"
                    "public:\n"
                    "    void TearDown() override {\n"
                    "        setenv(\"GCOV_PREFIX\","
                    " \"/data/data/com.swappy.testapp/files\", 1);\n"
                    "        setenv(\"GCOV_PREFIX_STRIP\", \"20\", 1);\n"
                    "        if (Swappy_flushCoverage) {\n"
                    "            Swappy_flushCoverage();\n"
                    "        }\n"
                    "        if (__gcov_dump) {\n"
                    "            __gcov_dump();\n"
                    "        }\n"
                    "        if (__gcov_flush) {\n"
                    "            __gcov_flush();\n"
                    "        }\n"
                    "    }\n"
                    "};\n"
                    "\n::testing::Environment* const g_gcov_flush_env =\n"
                    "    ::testing::AddGlobalTestEnvironment("
                    "new GcovFlushEnvironment);\n"
            )
            swappy_test_cpp.write_text(st_text + env_hook, encoding="utf-8")


def configure_gradle_for_coverage(
        gamesdk_dir: Path, module_dir: Path, stack: ExitStack
) -> None:
    """Injects C++ gcov coverage flags and runtime flush hooks for tests.

    Args:
        gamesdk_dir: Path to root gamesdk directory.
        module_dir: Path to specific module directory.
        stack: ExitStack managing temporary file backups.
    """
    repo_root = gamesdk_dir.parent
    ndk_prebuilt = repo_root / "prebuilts" / "sdk" / "ndk" / "27.0.12077973" if sys.platform == "darwin" else repo_root / "prebuilts" / "ndk" / "r27"

    _configure_root_gradle(gamesdk_dir, ndk_prebuilt, stack)

    for c_path in (module_dir, gamesdk_dir / "test" / "swappy"):
        if c_path.exists():
            for cmakelists in c_path.rglob("CMakeLists.txt"):
                stack.enter_context(ManagedFileBackup(cmakelists))
                c_content = cmakelists.read_text(encoding="utf-8")
                if "-Werror" in c_content:
                    c_content = c_content.replace("-Werror", "-Wno-error")
                    cmakelists.write_text(c_content, encoding="utf-8")

    for cxx_dir in [
            module_dir / ".cxx",
            gamesdk_dir / "test" / "swappy" / "testapp" / "app" / ".cxx",
    ]:
        if cxx_dir.exists():
            shutil.rmtree(cxx_dir)

    _configure_module_gradle_files(module_dir, ndk_prebuilt, stack)
    _inject_swappy_flush_hook(module_dir, stack)
    _configure_testapp_files(gamesdk_dir, ndk_prebuilt, stack)
    _inject_gcov_flush_environment(gamesdk_dir, stack)



def run_tests(gamesdk_dir: Path) -> None:
    """Builds test APK, installs on device, and executes GTest suite.

    Args:
        gamesdk_dir: Path to root gamesdk directory.

    Raises:
        FileNotFoundError: If test APK cannot be located.
        subprocess.CalledProcessError: If build or ADB commands fail.
    """
    repo_root = gamesdk_dir.parent

    print(f"\n[Step 1] Building test APK for '{MODULE_NAME}' with Gradle...")
    env = os.environ.copy()
    if "JAVA_HOME" not in env:
        java_21_path = Path("/usr/lib/jvm/java-21-openjdk-amd64")
        if java_21_path.exists():
            env["JAVA_HOME"] = str(java_21_path)

    cmake_bin = repo_root / "prebuilts" / "cmake" / ("darwin-x86" if sys.platform == "darwin" else "linux-x86") / "bin"
    ninja_bin = repo_root / "prebuilts" / "ninja" / ("darwin-x86" if sys.platform == "darwin" else "linux-x86")
    current_path = env.get("PATH", "")
    new_paths = [
            str(p) for p in (cmake_bin, ninja_bin) if p.exists()
    ]
    if new_paths:
        env["PATH"] = os.path.pathsep.join(new_paths + [current_path])

    testapp_dir = gamesdk_dir / "test" / "swappy" / "testapp"

    print(
            f"\n[Step 1] Building test APKs for '{MODULE_NAME}' (testapp) with"
            " Gradle..."
    )
    subprocess.run(
            ["./gradlew", "assembleDebug", "assembleDebugAndroidTest"],
            cwd=testapp_dir,
            env=env,
            check=True,
    )

    print("\n[Step 2] Installing test APKs on attached device...")
    app_apk = (
            testapp_dir
            / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk"
    )
    test_apk = (
            testapp_dir
            / "app"
            / "build"
            / "outputs"
            / "apk"
            / "androidTest"
            / "debug"
            / "app-debug-androidTest.apk"
    )
    if not app_apk.exists() or not test_apk.exists():
        raise FileNotFoundError("Test APKs for com.swappy.testapp not found.")

    print(f"Installing App APK: {app_apk}")
    subprocess.run(["adb", "install", "-r", str(app_apk)], check=True)
    print(f"Installing Test APK: {test_apk}")
    subprocess.run(["adb", "install", "-r", str(test_apk)], check=True)

    print(
            "\n[Step 3] Executing GTest suite on device via am instrument"
            f" ({TEST_PKG})..."
    )
    subprocess.run(
            [
                    "adb",
                    "shell",
                    "run-as",
                    APP_PKG,
                    "rm",
                    "-rf",
                    f"/data/data/{APP_PKG}/files/*",
            ],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
    )
    subprocess.run(
            [
                    "adb",
                    "shell",
                    "run-as",
                    APP_PKG,
                    "mkdir",
                    "-p",
                    f"/data/data/{APP_PKG}/files",
            ],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
    )

    result = subprocess.run(
            [
                    "adb",
                    "shell",
                    "am",
                    "instrument",
                    "-w",
                    f"{TEST_PKG}/androidx.test.runner.AndroidJUnitRunner",
            ],
            capture_output=True,
            text=True,
            check=False,
    )
    if (
            result.returncode != 0
            or "FAILURES" in result.stdout
            or "Process crashed" in result.stdout
    ):
        print(
            "Warning: Instrumentation test finished with code"
            f" {result.returncode}.\n{result.stdout}"
        )


def _sync_pulled_gcda_files(
        gcda_files: List[Path], cxx_dirs: List[Path]
) -> int:
    """Syncs runtime .gcda files to build dir .gcno locations."""
    synced = 0
    for gcda in gcda_files:
        gcno_filename = gcda.with_suffix(".gcno").name
        matching_gcno = []
        for cxx in cxx_dirs:
            if cxx.exists():
                matching_gcno.extend(
                        [
                            p for p in cxx.rglob(gcno_filename)
                            if "arm64-v8a" in p.parts
                        ]
                )
                if not matching_gcno:
                    matching_gcno.extend(list(cxx.rglob(gcno_filename)))
        for gcno in matching_gcno:
            dest_gcda = gcno.parent / gcda.name
            dest_gcda.write_bytes(gcda.read_bytes())
            synced += 1
    return synced


def pull_and_sync_device_gcov_data(module_dir: Path, out_dir: Path) -> None:
    """Pulls GCOV (.gcda) runtime coverage files and syncs to build dir.

    Args:
        module_dir: Path to module directory.
        out_dir: Output directory for temporary coverage extraction.
    """
    print("\n[Step 4] Pulling .gcda runtime coverage data...")
    temp_pull_dir = out_dir / "temp_gcov"
    if temp_pull_dir.exists():
        shutil.rmtree(temp_pull_dir)
    temp_pull_dir.mkdir(parents=True, exist_ok=True)

    archive_path = f"/data/data/{APP_PKG}/files/gcov_files.tar"
    local_tar = temp_pull_dir / "gcov_files.tar"

    subprocess.run(
            [
                    "adb",
                    "shell",
                    "run-as",
                    APP_PKG,
                    "tar",
                    "-cf",
                    archive_path,
                    "-C",
                    f"/data/data/{APP_PKG}/files",
                    ".",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
    )

    res = subprocess.run(
            ["adb", "exec-out", "run-as", APP_PKG, "cat", archive_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
    )

    if res.returncode == 0 and res.stdout:
        local_tar.write_bytes(res.stdout)
        try:
            with tarfile.open(local_tar) as tar:
                tar.extractall(path=temp_pull_dir)
        except (tarfile.TarError, OSError) as e:
            print(f"Tar extraction warning: {e}")

    gcda_files = list(temp_pull_dir.rglob("*.gcda"))
    print(f"Found {len(gcda_files)} .gcda runtime files pulled from device.")

    cxx_dirs = [
            module_dir / ".cxx",
            module_dir.parent / "test" / "swappy" / "testapp" / "app" / ".cxx",
    ]
    synced = _sync_pulled_gcda_files(gcda_files, cxx_dirs)

    print(f"Synced {synced} .gcda files to build output folders.")


def discover_source_files(module_dir: Path) -> Dict[str, str]:
    """Maps source file lower-case names to module-relative paths.

    Args:
        module_dir: Root directory of module.

    Returns:
        Dict mapping filename (lowercase) to relative path string.
    """
    target_map: Dict[str, str] = {}
    ignore_dirs = {".cxx", "build", "tools", "out", "androidTest"}
    for ext in ("*.cpp", "*.c", "*.h", "*.hpp"):
        for src in module_dir.rglob(ext):
            if any(part in src.parts for part in ignore_dirs):
                continue
            rel_path = src.relative_to(module_dir)
            target_map[src.name.lower()] = str(rel_path)
    return target_map


def _parse_gcov_file(
        gcov_file: Path,
        module_dir: Path,
        target_map: Dict[str, str],
        file_executed_lines: Dict[str, Dict[int, bool]],
) -> None:
    """Parses a .gcov output file and updates line execution dictionary."""
    lines = gcov_file.read_text(encoding="utf-8", errors="ignore").splitlines()
    if not lines:
        gcov_file.unlink()
        return

    source_rel: Optional[str] = None
    for line in lines[:15]:
        if "0:Source:" in line:
            source_str = line.split("0:Source:", 1)[1].strip()
            try:
                s_path = Path(source_str)
                if s_path.is_absolute() and module_dir in s_path.parents:
                    source_rel = str(s_path.relative_to(module_dir))
            except ValueError:
                pass
            break

    if not source_rel:
        stem = gcov_file.name.replace(".gcov", "").lower()
        source_rel = target_map.get(stem)

    if not source_rel or source_rel not in file_executed_lines:
        gcov_file.unlink()
        return

    for line in lines:
        parts = line.split(":", 2)
        if len(parts) >= 2:
            count = parts[0].strip()
            line_no = parts[1].strip()
            if line_no == "0" or not line_no.isdigit():
                continue
            l_idx = int(line_no)
            if count != "-":
                is_exec = count.isdigit() and int(count) > 0
                file_executed_lines[source_rel][l_idx] = (
                        file_executed_lines[source_rel].get(l_idx, False)
                        or is_exec
                )
    gcov_file.unlink()


def _find_active_gcno_files(cxx_dirs: List[Path]) -> List[Path]:
    """Collects active .gcno files with corresponding .gcda files."""
    all_gcno = []
    for cxx in cxx_dirs:
        if cxx.exists():
            all_gcno.extend(list(cxx.rglob("*.gcno")))
    gcno_files = [
            g
            for g in all_gcno
            if (g.parent / g.with_suffix(".gcda").name).exists()
            and (g.parent / g.with_suffix(".gcda").name).stat().st_size > 0
    ]
    if not gcno_files:
        print("Warning: No .gcno files with active .gcda data found.")
        gcno_files = all_gcno
    return gcno_files


def _process_gcno_files(
        gcno_files: List[Path],
        llvm_cov_bin: Path,
        module_dir: Path,
        target_map: Dict[str, str],
        file_executed_lines: Dict[str, Dict[int, bool]],
) -> None:
    """Runs llvm-cov gcov on .gcno files and parses resulting .gcov outputs."""
    print(
            f"\n[Step 5] Processing {len(gcno_files)} active .gcno build"
            " artifacts with llvm-cov gcov..."
    )
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_dir_path = Path(temp_dir)
        for idx, gcno in enumerate(gcno_files):
            sub_dir = temp_dir_path / f"target_{idx}"
            sub_dir.mkdir(exist_ok=True)
            subprocess.run(
                    [str(llvm_cov_bin), "gcov", str(gcno)],
                    cwd=sub_dir,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    check=False,
            )
            for gcov_file in sub_dir.glob("*.gcov"):
                _parse_gcov_file(
                    gcov_file, module_dir, target_map, file_executed_lines
                )


def generate_gcov_reports(
        module_dir: Path, llvm_cov_bin: Path
) -> List[CoverageSummary]:
    """Executes llvm-cov gcov and parses line-by-line execution results.

    Args:
        module_dir: Path to module directory.
        llvm_cov_bin: Path to llvm-cov binary.

    Returns:
        List of CoverageSummary items for all source files.
    """
    cxx_dirs = [
            module_dir / ".cxx",
            module_dir.parent / "test" / "swappy" / "testapp" / "app" / ".cxx",
    ]
    target_map = discover_source_files(module_dir)
    file_executed_lines: Dict[str, Dict[int, bool]] = {
            str(rel): {} for rel in target_map.values()
    }

    gcno_files = _find_active_gcno_files(cxx_dirs)
    _process_gcno_files(
            gcno_files,
            llvm_cov_bin,
            module_dir,
            target_map,
            file_executed_lines,
    )

    summary_rows: List[CoverageSummary] = []
    for rel_path, line_dict in sorted(file_executed_lines.items()):
        total_lines = len(line_dict)
        executed_lines = sum(1 for v in line_dict.values() if v)
        if total_lines > 0:
            summary_rows.append(
                    CoverageSummary(
                            relative_path=rel_path,
                            total_lines=total_lines,
                            executed_lines=executed_lines,
                    )
            )

    return summary_rows


def load_report_template(tools_dir: Path) -> Tuple[str, str]:
    """Loads HTML main template and row template.

    Args:
        tools_dir: Path to tools directory.

    Returns:
        Tuple of (main_template_string, row_template_string).

    Raises:
        FileNotFoundError: If template file does not exist.
    """
    template_file = tools_dir / "coverage_report_template.html"
    if not template_file.exists():
        raise FileNotFoundError(f"Template file {template_file} not found.")

    raw_content = template_file.read_text(encoding="utf-8")

    row_match = re.search(
            r"<!-- TABLE_ROW_TEMPLATE_START(.*?)TABLE_ROW_TEMPLATE_END -->",
            raw_content,
            re.DOTALL,
    )
    if row_match:
        row_template = row_match.group(1).strip()
        main_template = re.sub(
                r"<!-- TABLE_ROW_TEMPLATE_START.*?TABLE_ROW_TEMPLATE_END -->",
                "",
                raw_content,
                flags=re.DOTALL,
        ).strip()
    else:
        row_template = ""
        main_template = raw_content

    return main_template, row_template


def _get_badge_properties(pct: float) -> Tuple[str, str, str]:
    """Returns (fill, badge_cls, label) based on coverage percentage."""
    if pct >= 75.0:
        return "fill-green", "badge-success", "High"
    if pct >= 50.0:
        return "fill-yellow", "badge-warning", "Medium"
    return "fill-red", "badge-danger", "Low"


def _render_table_rows(
        summary_rows: List[CoverageSummary], row_template: str
) -> List[str]:
    """Renders individual HTML table rows for each file coverage summary."""
    rows_html: List[str] = []
    for row_data in summary_rows:
        fill_cls, badge_cls, badge_label = _get_badge_properties(
            row_data.percentage
        )
        row = (
                row_template.replace(
                    "{{ITEM_NAME_HTML}}", row_data.relative_path
                )
                .replace("{{TOTAL_LOC}}", f"{row_data.total_lines:,}")
                .replace("{{EXEC_LOC}}", f"{row_data.executed_lines:,}")
                .replace("{{COVERAGE_PCT}}", f"{row_data.percentage:.1f}")
                .replace("{{FILL_CLASS}}", fill_cls)
                .replace("{{BADGE_CLASS}}", badge_cls)
                .replace("{{BADGE_LABEL}}", badge_label)
        )
        rows_html.append(row)
    return rows_html


def generate_html_report(
        tools_dir: Path, gamesdk_dir: Path, summary_rows: List[CoverageSummary]
) -> None:
    """Generates single-page HTML coverage report from template.

    Args:
        tools_dir: Path to tools directory containing template.
        gamesdk_dir: Root gamesdk directory for report output.
        summary_rows: List of CoverageSummary objects.
    """
    reports_dir = gamesdk_dir / "out" / "reports" / "cxx_coverage"
    reports_dir.mkdir(parents=True, exist_ok=True)
    html_file = reports_dir / f"{MODULE_NAME}_coverage.html"

    main_template, row_template = load_report_template(tools_dir)

    total_loc = sum(r.total_lines for r in summary_rows)
    exec_loc = sum(r.executed_lines for r in summary_rows)
    overall_pct = (exec_loc / total_loc * 100.0) if total_loc > 0 else 0.0

    if total_loc > 0 and exec_loc == 0:
        print(
                "\nWARNING: No runtime execution data (.gcda) was"
                f" collected for '{MODULE_NAME}'."
        )

    rows_html = _render_table_rows(summary_rows, row_template)

    html_content = (
            main_template.replace("{{OVERALL_PCT}}", f"{overall_pct:.1f}")
            .replace("{{EXEC_LOC}}", f"{exec_loc:,}")
            .replace("{{TOTAL_LOC}}", f"{total_loc:,}")
            .replace("{{NUM_FILES}}", str(len(summary_rows)))
            .replace("{{TABLE_ROWS}}", "\n".join(rows_html))
    )

    html_file.write_text(html_content, encoding="utf-8")
    print(
            "\n[Step 6] HTML Coverage Report written to:\n"
            f"  file://{html_file.resolve()}\n"
    )


def _run_coverage_pipeline(
        gamesdk_dir: Path,
        module_dir: Path,
        tools_dir: Path,
        out_dir: Path,
        llvm_cov_bin: Path,
) -> None:
    """Runs the full stateless test coverage and HTML generation pipeline."""
    root_gradle = gamesdk_dir / "build.gradle"
    module_gradle = module_dir / "build.gradle"
    with ExitStack() as stack:
        stack.enter_context(ManagedFileBackup(root_gradle))
        stack.enter_context(ManagedFileBackup(module_gradle))
        for c_path in (module_dir, gamesdk_dir / "test" / "swappy"):
            if c_path.exists():
                for cmakelists in c_path.rglob("CMakeLists.txt"):
                    stack.enter_context(ManagedFileBackup(cmakelists))
        configure_gradle_for_coverage(gamesdk_dir, module_dir, stack)
        run_tests(gamesdk_dir)
        pull_and_sync_device_gcov_data(module_dir, out_dir)
        summary_rows = generate_gcov_reports(module_dir, llvm_cov_bin)
        generate_html_report(tools_dir, gamesdk_dir, summary_rows)


def main() -> None:
    """Main entry point for coverage generation script."""
    tools_dir = Path(__file__).resolve().parent
    module_dir = tools_dir.parent
    gamesdk_dir = module_dir.parent
    repo_root = gamesdk_dir.parent

    out_dir = gamesdk_dir / "out" / "coverage" / MODULE_NAME

    if sys.platform == "darwin":
        llvm_cov_bin = repo_root / "prebuilts" / "sdk" / "ndk" / "27.0.12077973" / "toolchains" / "llvm" / "prebuilt" / "darwin-x86_64" / "bin" / "llvm-cov"
    else:
        llvm_cov_bin = repo_root / "prebuilts" / "ndk" / "r27" / "toolchains" / "llvm" / "prebuilt" / "linux-x86_64" / "bin" / "llvm-cov"
    if not llvm_cov_bin.exists():
        sys.exit(f"Error: llvm-cov binary not found at {llvm_cov_bin}")

    _run_coverage_pipeline(
            gamesdk_dir, module_dir, tools_dir, out_dir, llvm_cov_bin
    )


if __name__ == "__main__":
    main()
