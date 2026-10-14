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
from typing import Dict, List, Optional, Tuple

MODULE_NAME = "games-frame-pacing"
TEST_PKG = "com.swappy.testapp.test"
APP_PKG = "com.swappy.testapp"

# Default NDK version matching ndk_version.gradle.
DEFAULT_NDK_VERSION = "27.0.12077973"
# Timeout in seconds for Gradle build/test invocations.
GRADLE_TIMEOUT_SECONDS = 600
# Timeout in seconds for ADB install/instrument/pull invocations.
ADB_TIMEOUT_SECONDS = 120
# Timeout in seconds for single-file llvm-cov gcov invocations.
LLVM_COV_TIMEOUT_SECONDS = 30
# Byte slice for the 32-bit stamp in GCC/LLVM .gcno and .gcda headers.
GCOV_STAMP_OFFSET = 8
GCOV_STAMP_END = 12


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


def _resolve_prebuilts_dir(repo_root: Path) -> Path:
    """Resolves the prebuilts directory across standalone and AOSP checkouts."""
    env_prebuilts = os.environ.get("PREBUILTS_DIR")
    if env_prebuilts:
        return Path(env_prebuilts)
    standalone_prebuilts = repo_root / "prebuilts"
    if standalone_prebuilts.exists():
        return standalone_prebuilts
    if len(repo_root.parents) >= 2:
        aosp_prebuilts = repo_root.parents[1] / "prebuilts"
        if aosp_prebuilts.exists():
            return aosp_prebuilts
    return standalone_prebuilts


def _resolve_ndk_dir(repo_root: Path) -> Path:
    """Resolves the prebuilt NDK directory across AOSP and standalone layouts."""
    ndk_env = os.environ.get("ANDROID_NDK_HOME")
    if ndk_env and Path(ndk_env).exists():
        return Path(ndk_env)
    prebuilts_dir = _resolve_prebuilts_dir(repo_root)
    sdk_ndk = prebuilts_dir / "sdk" / "ndk" / DEFAULT_NDK_VERSION
    if sys.platform == "darwin":
        return sdk_ndk
    for candidate in (
            prebuilts_dir / "ndk" / "r27",
            prebuilts_dir / "ndk" / "current",
            sdk_ndk,
    ):
        if candidate.exists():
            return candidate
    return sdk_ndk


def _get_cxx_dirs(
        module_dir: Path, include_workspace_temp: bool = True
) -> List[Path]:
    """Returns candidate native build directories containing .gcno/.gcda files."""
    gamesdk_dir = module_dir.parent
    cxx_dirs = [
            module_dir / ".cxx",
            gamesdk_dir / "test" / "swappy" / "testapp" / "app" / ".cxx",
            gamesdk_dir / "host_cmake",
    ]
    if include_workspace_temp:
        cxx_dirs.append(gamesdk_dir.parent / ".temp" / "host" / ".cmake")
    return cxx_dirs


def _ensure_testapp_local_properties(gamesdk_dir: Path) -> None:
    """Writes untracked local.properties for the standalone testapp build."""
    repo_root = gamesdk_dir.parent
    prebuilts_dir = _resolve_prebuilts_dir(repo_root)
    testapp_dir = gamesdk_dir / "test" / "swappy" / "testapp"
    local_props = testapp_dir / "local.properties"
    sdk_dir = prebuilts_dir / "sdk"
    ndk_dir = _resolve_ndk_dir(repo_root)
    host_tag = "darwin-x86" if sys.platform == "darwin" else "linux-x86"
    cmake_dir = prebuilts_dir / "cmake" / host_tag
    lines = []
    if sdk_dir.exists():
        lines.append(f"sdk.dir={sdk_dir.resolve()}")
    if ndk_dir.exists():
        lines.append(f"ndk.dir={ndk_dir.resolve()}")
    if cmake_dir.exists():
        lines.append(f"cmake.dir={cmake_dir.resolve()}")
    local_props.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _has_adb_device() -> bool:
    """Returns True if an online ADB device is attached."""
    try:
        res = subprocess.run(
                ["adb", "shell", "getprop", "ro.product.cpu.abi"],
                capture_output=True,
                text=True,
                check=False,
                timeout=5,
        )
        return res.returncode == 0 and bool(res.stdout.strip())
    except (OSError, subprocess.SubprocessError):
        return False


def run_tests(gamesdk_dir: Path) -> None:
    """Builds host/device tests with coverage enabled and executes GTest suites.

    Args:
        gamesdk_dir: Path to root gamesdk directory.

    Raises:
        FileNotFoundError: If test APK cannot be located.
        subprocess.CalledProcessError: If build or ADB commands fail.
    """
    repo_root = gamesdk_dir.parent
    prebuilts_dir = _resolve_prebuilts_dir(repo_root)

    env = os.environ.copy()
    if "JAVA_HOME" not in env:
        for candidate in (
                prebuilts_dir / "jdk" / "jdk21" / "linux-x86",
                prebuilts_dir / "jdk" / "jdk17" / "linux-x86",
                Path("/usr/lib/jvm/java-21-openjdk-amd64"),
        ):
            if candidate.exists():
                env["JAVA_HOME"] = str(candidate)
                break

    host_tag = "darwin-x86" if sys.platform == "darwin" else "linux-x86"
    cmake_bin = prebuilts_dir / "cmake" / host_tag / "bin"
    ninja_bin = prebuilts_dir / "ninja" / host_tag
    current_path = env.get("PATH", "")
    new_paths = [str(p) for p in (cmake_bin, ninja_bin) if p.exists()]
    if new_paths:
        env["PATH"] = os.path.pathsep.join(new_paths + [current_path])

    host_cmake_dir = repo_root / ".temp" / "host" / ".cmake"
    if host_cmake_dir.exists():
        for stale_gcda in host_cmake_dir.rglob("*.gcda"):
            stale_gcda.unlink(missing_ok=True)

    print("\n[Step 1] Running host C++ unit tests with coverage enabled...")
    subprocess.run(
            [
                    "./gradlew",
                    "hostUnitTests",
                    "-PenableCoverage=true",
            ],
            cwd=gamesdk_dir,
            env=env,
            check=True,
            timeout=GRADLE_TIMEOUT_SECONDS,
    )

    if not _has_adb_device():
        print(
                "\nNotice: No attached ADB device detected; using host coverage"
                " only."
        )
        return

    _ensure_testapp_local_properties(gamesdk_dir)
    testapp_dir = gamesdk_dir / "test" / "swappy" / "testapp"

    print(
            f"\n[Step 1b] Building test APKs for '{MODULE_NAME}' (testapp) with"
            " -PenableCoverage=true..."
    )
    subprocess.run(
            [
                    "./gradlew",
                    "assembleDebug",
                    "assembleDebugAndroidTest",
                    "-PenableCoverage=true",
            ],
            cwd=testapp_dir,
            env=env,
            check=True,
            timeout=GRADLE_TIMEOUT_SECONDS,
    )
    print("\n[Step 2] Installing test APKs on attached device...")
    app_apk = (
            testapp_dir
            / "app"
            / "build"
            / "outputs"
            / "apk"
            / "debug"
            / "app-debug.apk"
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
    subprocess.run(
            ["adb", "install", "-r", str(app_apk)],
            check=True,
            timeout=ADB_TIMEOUT_SECONDS,
    )
    print(f"Installing Test APK: {test_apk}")
    subprocess.run(
            ["adb", "install", "-r", str(test_apk)],
            check=True,
            timeout=ADB_TIMEOUT_SECONDS,
    )

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
            timeout=ADB_TIMEOUT_SECONDS,
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
            timeout=ADB_TIMEOUT_SECONDS,
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
            timeout=ADB_TIMEOUT_SECONDS,
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


def _read_gcov_stamp(filepath: Path) -> Optional[bytes]:
    """Reads the 4-byte stamp from a .gcno or .gcda header."""
    try:
        with filepath.open("rb") as f:
            header = f.read(GCOV_STAMP_END)
        if len(header) >= GCOV_STAMP_END:
            return header[GCOV_STAMP_OFFSET:GCOV_STAMP_END]
    except OSError:
        pass
    return None


def _sync_pulled_gcda_files(
        gcda_files: List[Path], cxx_dirs: List[Path]
) -> int:
    """Syncs runtime .gcda files to build dir .gcno locations with matching stamps."""
    synced = 0
    for gcda in gcda_files:
        gcda_stamp = _read_gcov_stamp(gcda)
        if gcda_stamp is None:
            continue
        gcno_filename = gcda.with_suffix(".gcno").name
        candidates: List[Path] = []
        for cxx in cxx_dirs:
            if cxx.exists():
                candidates.extend(cxx.rglob(gcno_filename))

        matching_gcno = [
                c for c in candidates if _read_gcov_stamp(c) == gcda_stamp
        ]
        if not matching_gcno:
            continue

        gcda_bytes = gcda.read_bytes()
        for gcno in matching_gcno:
            dest_gcda = gcno.parent / gcda.name
            if dest_gcda.resolve() != gcda.resolve():
                dest_gcda.write_bytes(gcda_bytes)
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
            timeout=ADB_TIMEOUT_SECONDS,
    )

    res = subprocess.run(
            ["adb", "exec-out", "run-as", APP_PKG, "cat", archive_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            check=False,
            timeout=ADB_TIMEOUT_SECONDS,
    )

    if res.returncode == 0 and res.stdout:
        local_tar.write_bytes(res.stdout)
        try:
            with tarfile.open(local_tar) as tar:
                tar.extractall(path=temp_pull_dir, filter="data")
        except (tarfile.TarError, OSError) as e:
            print(f"Tar extraction warning: {e}")

    gcda_files = list(temp_pull_dir.rglob("*.gcda"))
    print(f"Found {len(gcda_files)} .gcda runtime files pulled from device.")

    cxx_dirs = _get_cxx_dirs(module_dir)
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
    all_gcno: List[Path] = []
    for cxx in cxx_dirs:
        if cxx.exists():
            all_gcno.extend(cxx.rglob("*.gcno"))
    gcno_files = [
            g
            for g in all_gcno
            if g.with_suffix(".gcda").exists()
            and g.with_suffix(".gcda").stat().st_size > 0
    ]
    if not gcno_files:
        print("Warning: No .gcno files with active .gcda data found.")
        return all_gcno

    active_names = {g.name for g in gcno_files}
    active_abis = [
            abi
            for abi in ("arm64-v8a", "x86_64")
            if any(abi in g.parts for g in gcno_files)
    ]
    fallback_groups = [
            [g for g in all_gcno if abi in g.parts]
            for abi in (active_abis or ["x86_64", "arm64-v8a"])
    ] + [all_gcno]
    for group in fallback_groups:
        if group:
            for g in group:
                if g.name not in active_names:
                    gcno_files.append(g)
                    active_names.add(g.name)
            break
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
                    timeout=LLVM_COV_TIMEOUT_SECONDS,
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
    cxx_dirs = _get_cxx_dirs(module_dir)
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
    run_tests(gamesdk_dir)
    if _has_adb_device():
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
    host_tag = "darwin-x86_64" if sys.platform == "darwin" else "linux-x86_64"
    llvm_cov_bin = (
            _resolve_ndk_dir(repo_root)
            / "toolchains"
            / "llvm"
            / "prebuilt"
            / host_tag
            / "bin"
            / "llvm-cov"
    )
    if not llvm_cov_bin.exists():
        sys.exit(f"Error: llvm-cov binary not found at {llvm_cov_bin}")

    _run_coverage_pipeline(
            gamesdk_dir, module_dir, tools_dir, out_dir, llvm_cov_bin
    )


if __name__ == "__main__":
    main()
