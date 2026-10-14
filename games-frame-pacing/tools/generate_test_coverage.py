#!/usr/bin/env python3
"""Automated C++ test coverage generator for games-frame-pacing (Swappy).

Usage:
        python3 games-frame-pacing/tools/generate_test_coverage.py
"""

import argparse
from dataclasses import dataclass, field
import html
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from typing import Dict, List, Optional, Tuple
import zipfile

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
    line_counts: Dict[int, int] = field(default_factory=dict)

    @property
    def detail_html_filename(self) -> str:
        """Returns relative HTML filename for per-file source view."""
        sanitized = (
                self.relative_path.replace("/", "__")
                .replace("\\", "__")
                .replace("..", "_")
        )
        return f"sources/{sanitized}.html"

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


def _resolve_module_relative_path(
        source_str: str,
        module_dir: Path,
        target_map: Dict[str, str],
) -> Optional[str]:
    """Resolves a compile-time source path from gcov to a module-relative path."""
    normalized = os.path.normpath(source_str.strip()).replace("\\", "/")
    module_norm = os.path.normpath(str(module_dir)).replace("\\", "/")
    valid_rels = set(target_map.values())
    if normalized.startswith(module_norm + "/"):
        rel = normalized[len(module_norm) + 1 :]
        if rel in valid_rels:
            return rel

    marker = f"/{MODULE_NAME}/"
    if marker in normalized:
        rel = normalized.split(marker, 1)[1]
        if rel in valid_rels:
            return rel

    if "/" not in normalized:
        return target_map.get(Path(normalized).name.lower())
    return None


def _parse_gcov_file(
        gcov_file: Path,
        module_dir: Path,
        target_map: Dict[str, str],
        file_executed_lines: Dict[str, Dict[int, int]],
) -> None:
    """Parses a standard or intermediate (-i) .gcov file into line counts."""
    lines = gcov_file.read_text(encoding="utf-8", errors="ignore").splitlines()
    gcov_file.unlink(missing_ok=True)
    if not lines:
        return

    if any(line.startswith("file:") for line in lines[:5]):
        current_rel: Optional[str] = None
        for line in lines:
            if line.startswith("file:"):
                current_rel = _resolve_module_relative_path(
                        line.split("file:", 1)[1], module_dir, target_map
                )
            elif line.startswith("lcount:") and current_rel in file_executed_lines:
                parts = line.split("lcount:", 1)[1].split(",")
                if len(parts) >= 2 and parts[0].isdigit():
                    l_idx = int(parts[0])
                    hit_count = int(parts[1]) if parts[1].isdigit() else 0
                    if l_idx > 0:
                        file_executed_lines[current_rel][l_idx] = (
                                file_executed_lines[current_rel].get(l_idx, 0)
                                + hit_count
                        )
        return

    source_rel: Optional[str] = None
    has_source_header = False
    for line in lines[:15]:
        if "0:Source:" in line:
            has_source_header = True
            source_rel = _resolve_module_relative_path(
                    line.split("0:Source:", 1)[1], module_dir, target_map
            )
            break

    if not source_rel and not has_source_header:
        stem = gcov_file.name.removesuffix(".gcov").lower()
        source_rel = target_map.get(stem)

    if not source_rel or source_rel not in file_executed_lines:
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
                hit_count = int(count) if count.isdigit() else 0
                file_executed_lines[source_rel][l_idx] = (
                        file_executed_lines[source_rel].get(l_idx, 0)
                        + hit_count
                )


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
        file_executed_lines: Dict[str, Dict[int, int]],
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
                    [str(llvm_cov_bin), "gcov", "-i", str(gcno)],
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
        module_dir: Path,
        llvm_cov_bin: Path,
        cxx_dirs: Optional[List[Path]] = None,
) -> List[CoverageSummary]:
    """Executes llvm-cov gcov and parses line-by-line execution results.

    Args:
        module_dir: Path to module directory.
        llvm_cov_bin: Path to llvm-cov binary.
        cxx_dirs: Optional override list of native build directories.

    Returns:
        List of CoverageSummary items for all source files.
    """
    resolved_cxx_dirs = (
            cxx_dirs if cxx_dirs is not None else _get_cxx_dirs(module_dir)
    )
    target_map = discover_source_files(module_dir)
    file_executed_lines: Dict[str, Dict[int, int]] = {
            str(rel): {} for rel in target_map.values()
    }

    gcno_files = _find_active_gcno_files(resolved_cxx_dirs)
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
        executed_lines = sum(1 for v in line_dict.values() if v > 0)
        if total_lines > 0:
            summary_rows.append(
                    CoverageSummary(
                            relative_path=rel_path,
                            total_lines=total_lines,
                            executed_lines=executed_lines,
                            line_counts=dict(line_dict),
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
        escaped_path = html.escape(row_data.relative_path)
        escaped_href = html.escape(row_data.detail_html_filename, quote=True)
        item_link = f'<a href="{escaped_href}">{escaped_path}</a>'
        row = (
                row_template.replace("{{ITEM_NAME_HTML}}", item_link)
                .replace("{{TOTAL_LOC}}", f"{row_data.total_lines:,}")
                .replace("{{EXEC_LOC}}", f"{row_data.executed_lines:,}")
                .replace("{{COVERAGE_PCT}}", f"{row_data.percentage:.1f}")
                .replace("{{FILL_CLASS}}", fill_cls)
                .replace("{{BADGE_CLASS}}", badge_cls)
                .replace("{{BADGE_LABEL}}", badge_label)
        )
        rows_html.append(row)
    return rows_html


def _render_source_coverage_html(
        row_data: CoverageSummary, source_text: str
) -> str:
    """Renders standalone dark-mode HTML view for a single source file."""
    escaped_title = html.escape(row_data.relative_path)
    line_rows: List[str] = []
    for idx, raw_line in enumerate(source_text.splitlines(), start=1):
        escaped_code = html.escape(raw_line)
        count = row_data.line_counts.get(idx)
        if count is None:
            row_cls = "line-neutral"
            count_str = ""
        elif count > 0:
            row_cls = "line-covered"
            count_str = f"{count}x"
        else:
            row_cls = "line-missed"
            count_str = "0x"
        line_rows.append(
                f'<tr class="{row_cls}">'
                f'<td class="ln">{idx}</td>'
                f'<td class="cnt">{count_str}</td>'
                f'<td class="code"><pre>{escaped_code}</pre></td>'
                "</tr>"
        )
    rows_joined = "\n".join(line_rows)
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>{escaped_title} - Coverage</title>
  <style>
    body {{ background:#0f172a; color:#f8fafc; font-family:ui-monospace,monospace; margin:0; padding:24px; }}
    a {{ color:#38bdf8; text-decoration:none; }}
    .header {{ margin-bottom:16px; padding-bottom:12px; border-bottom:1px solid #334155; }}
    .meta {{ color:#94a3b8; font-size:13px; margin-top:6px; }}
    table {{ width:100%; border-collapse:collapse; font-size:12px; background:#1e293b; }}
    td {{ padding:2px 8px; vertical-align:top; }}
    td.ln {{ width:48px; text-align:right; color:#64748b; user-select:none; border-right:1px solid #334155; }}
    td.cnt {{ width:56px; text-align:right; color:#94a3b8; user-select:none; border-right:1px solid #334155; font-weight:600; }}
    td.code pre {{ margin:0; white-space:pre-wrap; word-break:break-all; }}
    tr.line-covered {{ background:rgba(34, 197, 94, 0.14); }}
    tr.line-covered td.cnt {{ color:#4ade80; }}
    tr.line-missed {{ background:rgba(239, 68, 68, 0.18); }}
    tr.line-missed td.cnt {{ color:#f87171; }}
  </style>
</head>
<body>
  <div class="header">
    <div><a href="../{MODULE_NAME}_coverage.html">&larr; Back to {MODULE_NAME} Summary</a></div>
    <h1 style="font-size:18px;margin:8px 0 0 0;">{escaped_title}</h1>
    <div class="meta">Coverage: <strong>{row_data.percentage:.1f}%</strong> ({row_data.executed_lines:,} / {row_data.total_lines:,} executable lines)</div>
  </div>
  <table>
    <tbody>
{rows_joined}
    </tbody>
  </table>
</body>
</html>
"""


def generate_html_report(
        tools_dir: Path,
        gamesdk_dir: Path,
        summary_rows: List[CoverageSummary],
        output_dir: Optional[Path] = None,
) -> Path:
    """Generates single-page HTML coverage report from template.

    Args:
        tools_dir: Path to tools directory containing template.
        gamesdk_dir: Root gamesdk directory for report output.
        summary_rows: List of CoverageSummary objects.
        output_dir: Optional custom output directory for reports.

    Returns:
        Path to the generated index HTML report.
    """
    reports_dir = (
            output_dir
            if output_dir is not None
            else (gamesdk_dir / "out" / "reports" / "cxx_coverage")
    )
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

    module_dir = gamesdk_dir / MODULE_NAME
    sources_dir = reports_dir / "sources"
    sources_dir.mkdir(parents=True, exist_ok=True)
    for row_data in summary_rows:
        src_file = module_dir / row_data.relative_path
        if src_file.exists():
            src_text = src_file.read_text(encoding="utf-8", errors="replace")
        else:
            print(f"Warning: Source file not found for detail view: {src_file}")
            src_text = f"// Source file not found at {src_file}\n"
        detail_file = reports_dir / row_data.detail_html_filename
        detail_file.parent.mkdir(parents=True, exist_ok=True)
        detail_file.write_text(
                _render_source_coverage_html(row_data, src_text),
                encoding="utf-8",
        )
    print(
            "\n[Step 6] HTML Coverage Report written to:\n"
            f"  file://{html_file.resolve()}\n"
    )
    return html_file


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


def package_gcno_bundle(
        gamesdk_dir: Path, module_dir: Path, bundle_path: Path
) -> Path:
    """Packages compile-time .gcno notes, module sources, and report tooling."""
    bundle_path.parent.mkdir(parents=True, exist_ok=True)
    cxx_dirs = _get_cxx_dirs(module_dir)
    gcno_count = 0
    with tarfile.open(bundle_path, "w:gz") as tar:
        for cxx in cxx_dirs:
            if not cxx.exists():
                continue
            for gcno in cxx.rglob("*.gcno"):
                try:
                    rel = gcno.relative_to(gamesdk_dir)
                except ValueError:
                    rel = Path("host_cmake") / gcno.relative_to(cxx)
                tar.add(gcno, arcname=str(rel))
                gcno_count += 1

        target_map = discover_source_files(module_dir)
        for rel_src in target_map.values():
            src_path = module_dir / rel_src
            if src_path.exists():
                tar.add(src_path, arcname=f"{MODULE_NAME}/{rel_src}")

        for tool_name in (
                "coverage_report_template.html",
                "generate_test_coverage.py",
        ):
            tool_path = module_dir / "tools" / tool_name
            if tool_path.exists():
                tar.add(
                        tool_path,
                        arcname=f"{MODULE_NAME}/tools/{tool_name}",
                )

    print(f"Packaged {gcno_count} .gcno files into {bundle_path}")
    return bundle_path


def _collect_gcda_files(gcda_dir: Path, scratch_dir: Path) -> List[Path]:
    """Collects .gcda files from a directory and any nested TradeFed .zip archives."""
    gcda_files = list(gcda_dir.rglob("*.gcda"))
    for idx, zip_path in enumerate(gcda_dir.rglob("*.zip")):
        extract_dir = scratch_dir / f"zip_{idx}"
        extract_dir.mkdir(parents=True, exist_ok=True)
        try:
            with zipfile.ZipFile(zip_path, "r") as zf:
                for member in zf.infolist():
                    member_path = Path(member.filename)
                    if ".." in member_path.parts or member_path.is_absolute():
                        continue
                    if member.filename.endswith(".gcda"):
                        zf.extract(member, path=extract_dir)
            gcda_files.extend(extract_dir.rglob("*.gcda"))
        except (zipfile.BadZipFile, OSError) as e:
            print(f"Warning: Failed to extract {zip_path}: {e}")
    return gcda_files


def run_report_from_artifacts(
        gamesdk_dir: Path,
        module_dir: Path,
        tools_dir: Path,
        llvm_cov_bin: Path,
        gcda_dir: Optional[Path],
        bundle_path: Optional[Path],
        output_dir: Path,
) -> Path:
    """Generates coverage HTML report from a .gcno bundle and/or .gcda dir.

    Raises:
        FileNotFoundError: If a specified bundle_path or gcda_dir does not exist.
    """
    if bundle_path is not None and not bundle_path.is_file():
        raise FileNotFoundError(f"Coverage bundle not found: {bundle_path}")
    if gcda_dir is not None and not gcda_dir.exists():
        raise FileNotFoundError(f"gcda directory not found: {gcda_dir}")

    with tempfile.TemporaryDirectory() as tmp_str:
        tmp_root = Path(tmp_str)
        work_gamesdk = gamesdk_dir
        work_module = module_dir
        work_tools = tools_dir

        if bundle_path is not None:
            work_gamesdk = tmp_root / "bundle_root"
            work_gamesdk.mkdir(parents=True, exist_ok=True)
            with tarfile.open(bundle_path, "r:*") as tar:
                tar.extractall(path=work_gamesdk, filter="data")
            work_module = work_gamesdk / MODULE_NAME
            work_tools = work_module / "tools"
            cxx_dirs = _get_cxx_dirs(work_module, include_workspace_temp=False)
        elif gcda_dir is not None and any(gcda_dir.rglob("*.gcno")):
            cxx_dirs = [gcda_dir]
        else:
            cxx_dirs = _get_cxx_dirs(work_module)

        if gcda_dir is not None:
            pulled_gcda = _collect_gcda_files(gcda_dir, tmp_root / "unzipped")
            _sync_pulled_gcda_files(pulled_gcda, cxx_dirs)

        summary_rows = generate_gcov_reports(
                work_module, llvm_cov_bin, cxx_dirs=cxx_dirs
        )
        return generate_html_report(
                work_tools, work_gamesdk, summary_rows, output_dir=output_dir
        )


def _find_llvm_cov_binary(
        repo_root: Path, explicit_path: Optional[Path] = None
) -> Path:
    """Locates llvm-cov from explicit CLI arg, NDK/Clang prebuilts, or PATH."""
    if explicit_path is not None:
        if not explicit_path.exists():
            raise FileNotFoundError(
                    f"llvm-cov binary not found at {explicit_path}"
            )
        return explicit_path

    host_tag = "darwin-x86_64" if sys.platform == "darwin" else "linux-x86_64"
    candidates: List[Path] = []
    if cc_env := os.environ.get("CC"):
        candidates.append(Path(cc_env).with_name("llvm-cov"))
    candidates.append(
            _resolve_ndk_dir(repo_root)
            / "toolchains"
            / "llvm"
            / "prebuilt"
            / host_tag
            / "bin"
            / "llvm-cov"
    )
    prebuilts_dir = _resolve_prebuilts_dir(repo_root)
    clang_host = prebuilts_dir / "clang" / "host" / (
            "darwin-x86" if sys.platform == "darwin" else "linux-x86"
    )
    if clang_host.exists():
        for clang_dir in sorted(clang_host.glob("clang-r*"), reverse=True):
            candidates.append(clang_dir / "bin" / "llvm-cov")

    for candidate in candidates:
        if candidate.exists():
            return candidate
    if which_cov := shutil.which("llvm-cov"):
        return Path(which_cov)
    raise FileNotFoundError(
            "llvm-cov binary not found in prebuilts or PATH; pass --llvm-cov."
    )


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    """Parses command-line arguments."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
            "--mode",
            choices=("local", "package-gcno", "report"),
            default="local",
            help="Execution mode (default: local).",
    )
    parser.add_argument(
            "--bundle-path",
            type=Path,
            default=None,
            help="Path to swappy_coverage_bundle.tar.gz.",
    )
    parser.add_argument(
            "--dist-dir",
            type=Path,
            default=None,
            help="Distribution directory for swappy_coverage_bundle.tar.gz.",
    )
    parser.add_argument(
            "--gcda-dir",
            type=Path,
            default=None,
            help="Directory containing runtime .gcda files or TradeFed .zip archives.",
    )
    parser.add_argument(
            "--output-dir",
            type=Path,
            default=None,
            help="Output directory for HTML coverage reports.",
    )
    parser.add_argument(
            "--llvm-cov",
            type=Path,
            default=None,
            help="Explicit path to llvm-cov binary.",
    )
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> None:
    """Main entry point for coverage generation script."""
    args = parse_args(argv)
    tools_dir = Path(__file__).resolve().parent
    module_dir = tools_dir.parent
    gamesdk_dir = module_dir.parent
    repo_root = gamesdk_dir.parent

    if args.mode == "package-gcno":
        if args.bundle_path:
            bundle_out = args.bundle_path
        elif args.dist_dir:
            bundle_out = args.dist_dir / "swappy_coverage_bundle.tar.gz"
        else:
            bundle_out = (
                    gamesdk_dir / "out" / "dist" / "swappy_coverage_bundle.tar.gz"
            )
        package_gcno_bundle(gamesdk_dir, module_dir, bundle_out)
        return

    out_dir = gamesdk_dir / "out" / "coverage" / MODULE_NAME
    try:
        llvm_cov_bin = _find_llvm_cov_binary(repo_root, args.llvm_cov)
    except FileNotFoundError as err:
        sys.exit(f"Error: {err}")

    if args.mode == "report":
        report_out = args.output_dir or (
                gamesdk_dir / "out" / "reports" / "cxx_coverage"
        )
        run_report_from_artifacts(
                gamesdk_dir=gamesdk_dir,
                module_dir=module_dir,
                tools_dir=tools_dir,
                llvm_cov_bin=llvm_cov_bin,
                gcda_dir=args.gcda_dir,
                bundle_path=args.bundle_path,
                output_dir=report_out,
        )
        return

    _run_coverage_pipeline(
            gamesdk_dir, module_dir, tools_dir, out_dir, llvm_cov_bin
    )


if __name__ == "__main__":
    main()
