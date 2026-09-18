/*
 * Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Calibration.h"

#define LOG_TAG "SwappyTest"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "DvfsBenchmark.h"
#include "DvfsBenchmarkInternal.h"
#include "Log.h"

namespace dvfs {

// The calibration cache format. Version 2 stored the CPU and GPU rates of a
// render API in one combined cache; version 3 stores each kind on its own.
static constexpr int kCalibrationCacheVersion = 3;

// The version 2 GLES cache. The version 2 Vulkan cache has the name of the
// version 3 one and is told apart by its version.
static constexpr char kObsoleteCombinedCacheFilename[] = "swappy_calibration.json";

static const char* calibration_cache_filename(CalibrationKind kind) {
    switch (kind) {
        case CALIBRATION_CPU:
            return "swappy_calibration_cpu.json";
        case CALIBRATION_GLES:
            return "swappy_calibration_gles.json";
        default:
            return "swappy_calibration_vulkan.json";
    }
}

static std::string calibration_cache_path(const char* filename) {
    std::lock_guard<std::mutex> lock(s_test_mutex);
    return output_path(filename);
}

// Logs once per process and path that the cache at path is obsolete.
static void log_obsolete_cache_once(const std::string& path) {
    static std::mutex mutex;
    static std::set<std::string> logged;
    std::lock_guard<std::mutex> lock(mutex);
    if (logged.insert(path).second) {
        ALOGW("SwappyTest: %s is an obsolete combined CPU+GPU calibration cache and is ignored; "
              "run the CPU and GPU calibrations again.",
              path.c_str());
    }
}

using CacheValues = std::map<std::string, std::string>;

// The key=value lines of the cache at path; false when it does not exist.
static bool read_cache_file(const std::string& path, CacheValues* values) {
    FILE* fp = fopen(path.c_str(), "r");
    if (!fp) return false;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char key[64] = {0};
        char val[256] = {0};
        if (sscanf(line, " %63[^=]=%255s", key, val) == 2) (*values)[key] = val;
    }
    fclose(fp);
    return true;
}

static std::string cache_value(const CacheValues& values, const char* key) {
    const auto it = values.find(key);
    return it == values.end() ? std::string() : it->second;
}

static int cache_int(const CacheValues& values, const char* key, int fallback) {
    const std::string value = cache_value(values, key);
    return value.empty() ? fallback : atoi(value.c_str());
}

// Reads the cache of kind into values and checks what every cache must
// match: the cache version, the kind and the build. An obsolete cache counts
// as missing.
static CalibrationStatus read_calibration_cache(CalibrationKind kind, CacheValues* values) {
    const char* name = calibration_kind_name(kind);
    const std::string path = calibration_cache_path(calibration_cache_filename(kind));
    if (!read_cache_file(path, values)) {
        const std::string obsolete = calibration_cache_path(kObsoleteCombinedCacheFilename);
        if (kind != CALIBRATION_VULKAN && access(obsolete.c_str(), F_OK) == 0) {
            log_obsolete_cache_once(obsolete);
        }
        return CALIBRATION_STATUS_MISSING;
    }
    if (cache_int(*values, "calib_version", 0) != kCalibrationCacheVersion) {
        log_obsolete_cache_once(path);
        return CALIBRATION_STATUS_MISSING;
    }
    if (cache_value(*values, "calib_kind") != name) {
        ALOGW("SwappyTest: %s holds a '%s' calibration, not '%s'; recalibrate", path.c_str(),
              cache_value(*values, "calib_kind").c_str(), name);
        return CALIBRATION_STATUS_INVALID;
    }
    const std::string cached_fp = cache_value(*values, "build_fingerprint");
    const std::string current_fp = read_prop("ro.build.fingerprint");
    if (cached_fp.empty() || (!current_fp.empty() && current_fp != cached_fp)) {
        ALOGI("SwappyTest: Cached %s calibration fingerprint mismatch ('%s' vs '%s'), "
              "recalibrate",
              name, cached_fp.c_str(), current_fp.c_str());
        return CALIBRATION_STATUS_INVALID;
    }
    return CALIBRATION_STATUS_VALID;
}

CalibrationStatus load_cpu_calibration_cache(CpuCalibrationCache* cpu) {
    CacheValues values;
    const CalibrationStatus status = read_calibration_cache(CALIBRATION_CPU, &values);
    if (status != CALIBRATION_STATUS_VALID) return status;
    CpuCalibrationCache cache;
    cache.iters_per_ms =
            strtoull(cache_value(values, "calibrated_cpu_iters_per_ms").c_str(), nullptr, 10);
    cache.core = cache_int(values, "calib_core", -1);
    cache.freq_mhz = cache_int(values, "calib_freq_mhz", -1);
    const std::string mode = cache_value(values, "cpu_calib_mode");
    if (!mode.empty()) cache.mode = mode;
    cache.r_squared = strtod(cache_value(values, "cpu_r_squared").c_str(), nullptr);
    cache.rme95_pct = strtod(cache_value(values, "cpu_rme95_pct").c_str(), nullptr);
    cache.mean_error_pct = strtod(cache_value(values, "cpu_mean_error_pct").c_str(), nullptr);
    cache.accepted_chunks = cache_int(values, "cpu_accepted_chunks", 0);
    if (cache.iters_per_ms == 0) return CALIBRATION_STATUS_INVALID;
    *cpu = cache;
    return CALIBRATION_STATUS_VALID;
}

CalibrationStatus load_gpu_calibration_cache(CalibrationKind kind, int64_t expected_pixels,
                                             GpuCalibrationCache* gpu) {
    CacheValues values;
    const CalibrationStatus status = read_calibration_cache(kind, &values);
    if (status != CALIBRATION_STATUS_VALID) return status;
    GpuCalibrationCache cache;
    cache.iters_per_ms =
            strtod(cache_value(values, "calibrated_gpu_iters_per_ms").c_str(), nullptr);
    cache.fixed_ms = strtod(cache_value(values, "calibrated_gpu_fixed_ms").c_str(), nullptr);
    if (cache.iters_per_ms <= 0.0) return CALIBRATION_STATUS_INVALID;
    const int64_t surface_pixels =
            strtoll(cache_value(values, "surface_pixels").c_str(), nullptr, 10);
    if (expected_pixels > 0 && surface_pixels > 0 && surface_pixels != expected_pixels) {
        ALOGI("SwappyTest: Cached %s calibration surface pixel mismatch (cached %" PRId64
              " vs current %" PRId64 "), recalibrate",
              calibration_kind_name(kind), surface_pixels, expected_pixels);
        return CALIBRATION_STATUS_INVALID;
    }
    cache.pixels = surface_pixels;
    const std::string timer = cache_value(values, "gpu_timer");
    if (!timer.empty()) cache.timer = timer;
    cache.r_squared = strtod(cache_value(values, "gpu_r_squared").c_str(), nullptr);
    cache.rme95_pct = strtod(cache_value(values, "gpu_rme95_pct").c_str(), nullptr);
    cache.mean_error_pct = strtod(cache_value(values, "gpu_mean_error_pct").c_str(), nullptr);
    cache.accepted_samples = cache_int(values, "gpu_accepted_samples", 0);
    *gpu = cache;
    return CALIBRATION_STATUS_VALID;
}

// Stores the cache of kind: the lines every cache has, then body.
static void write_calibration_cache(CalibrationKind kind, const char* body) {
    const std::string path = calibration_cache_path(calibration_cache_filename(kind));
    FILE* fp = fopen(path.c_str(), "w");
    if (!fp) {
        ALOGW("SwappyTest: Failed to open %s for writing calibration cache", path.c_str());
        return;
    }
    fprintf(fp,
            "calib_version=%d\n"
            "calib_kind=%s\n"
            "build_fingerprint=%s\n"
            "device_model=%s\n"
            "%s",
            kCalibrationCacheVersion, calibration_kind_name(kind),
            read_prop("ro.build.fingerprint").c_str(), read_prop("ro.product.model").c_str(), body);
    fclose(fp);
    ALOGI("SwappyTest: Saved %s calibration cache v%d to %s", calibration_kind_name(kind),
          kCalibrationCacheVersion, path.c_str());
}

void save_cpu_calibration_cache(const CpuCalibrationResult& cpu, int global_max_mhz) {
    char body[512];
    snprintf(body, sizeof(body),
             "device_max_freq_mhz=%d\n"
             "calib_core=%d\n"
             "calib_freq_mhz=%d\n"
             "calibrated_cpu_iters_per_ms=%llu\n"
             "cpu_calib_mode=%s\n"
             "cpu_r_squared=%.6f\n"
             "cpu_rme95_pct=%.2f\n"
             "cpu_mean_error_pct=%.2f\n"
             "cpu_accepted_chunks=%d\n",
             global_max_mhz, cpu.core, cpu.freq_mhz, (unsigned long long)cpu.iters_per_ms,
             cpu.mode.c_str(), cpu.r_squared, cpu.rme95_pct, cpu.mean_error_pct,
             cpu.accepted_chunks);
    write_calibration_cache(CALIBRATION_CPU, body);
}

void save_gpu_calibration_cache(CalibrationKind kind, const GpuCalibrationResult& gpu,
                                int64_t surface_pixels) {
    char body[512];
    snprintf(body, sizeof(body),
             "render_api=%s\n"
             "surface_pixels=%" PRId64 "\n"
             "calibrated_gpu_iters_per_ms=%.4f\n"
             "calibrated_gpu_fixed_ms=%.4f\n"
             "gpu_timer=%s\n"
             "gpu_r_squared=%.6f\n"
             "gpu_rme95_pct=%.2f\n"
             "gpu_mean_error_pct=%.2f\n"
             "gpu_accepted_samples=%d\n",
             calibration_kind_name(kind), surface_pixels, gpu.iters_per_ms, gpu.fixed_ms,
             gpu.timer.c_str(), gpu.r_squared, gpu.rme95_pct, gpu.mean_error_pct,
             gpu.accepted_samples);
    write_calibration_cache(kind, body);
}

// Appends the report banner, the calibrated kinds, device identity and
// surface details.
static void append_report_header(std::string& report, CalibrationSet kinds,
                                 const GpuCalibrationResult& gpu) {
    const std::string fingerprint = read_prop("ro.build.fingerprint");
    const std::string model = read_prop("ro.product.model");

    int cur_w = s_render_surface_width.load(std::memory_order_relaxed);
    int cur_h = s_render_surface_height.load(std::memory_order_relaxed);
    int64_t surface_pix = static_cast<int64_t>(cur_w) * cur_h;
    if (surface_pix <= 0 && gpu.pixels > 0) {
        surface_pix = gpu.pixels;
    }

    char buf[1024];
    snprintf(buf, sizeof(buf),
             "================================================================================\n"
             "                      DVFS BENCHMARK CALIBRATION REPORT\n"
             "================================================================================\n"
             "Status: FRESH CALIBRATION\n"
             "Calibrations: %s\n"
             "Device Model: %s\n"
             "Build Fingerprint: %s\n"
             "Graphics API: %s\n"
             "Render API: %s\n"
             "Surface Size: %dx%d (%" PRId64 " pixels)\n"
             "Initial CPU Headroom: %d%%\n"
             "Final CPU Headroom: %d%%\n"
             "Cooldown Duration: %.1f ms\n\n",
             format_calibration_set(kinds).c_str(), model.c_str(), fingerprint.c_str(),
             (get_active_render_api() == SWAPPY_RENDER_API_VULKAN) ? "Vulkan" : "GLES",
             get_active_render_api_name(), cur_w, cur_h, surface_pix, s_headroom_start_pct.load(),
             s_headroom_end_pct.load(), s_cooldown_duration_ms);
    report += buf;
}

// Appends the per-level breakdown table, if any levels were measured; the
// rate column is printed with rate_precision decimals.
static void append_level_table(std::string& report, const std::vector<CalibrationLevelStat>& stats,
                               int rate_precision) {
    if (stats.empty()) return;
    report += "Per-Level Breakdown Table:\n";
    report += "Target_ms | Iters_N       | Mean_Meas_ms | Std_ms   | Mean_Err_% | Max_Err_%  | "
              "Rate_iters_ms | Samples\n";
    report += "----------+---------------+--------------+----------+------------+------------+-"
              "--------------+--------\n";
    char buf[1024];
    for (const auto& lvl : stats) {
        snprintf(buf, sizeof(buf),
                 "%-9.1f | %-13llu | %-12.4f | %-8.4f | %-10.2f | %-10.2f | %-13.*f | %-7d\n",
                 lvl.target_ms, (unsigned long long)lvl.iters_N, lvl.mean_meas_ms, lvl.std_ms,
                 lvl.mean_err_pct, lvl.max_err_pct, rate_precision, lvl.level_rate, lvl.samples);
        report += buf;
    }
    report += "\n";
}

static const char* cpu_calibration_verdict(const CpuCalibrationResult& cpu, bool held) {
    return cpu_calibration_failure(cpu, held).empty() ? "PASS" : "FAIL";
}

static const char* gpu_calibration_verdict(const GpuCalibrationResult& gpu, bool held) {
    return gpu_calibration_failure(gpu, held).empty() ? "PASS" : "FAIL";
}

static void append_cpu_section(std::string& report, const CpuCalibrationResult& cpu,
                               const char* cpu_verdict, const std::string& cpu_failure) {
    int total_cpu_chunks = cpu.accepted_chunks + cpu.rejected_migrated + cpu.rejected_cluster +
            cpu.rejected_preempted + cpu.rejected_freq + cpu.rejected_unknown;
    const char* detail = cpu.failure ? cpu.failure : cpu_failure.c_str();
    const bool has_detail = detail[0] != '\0';

    char buf[1024];
    snprintf(buf, sizeof(buf),
             "--------------------------------------------------------------------------------\n"
             "                              CPU CALIBRATION\n"
             "--------------------------------------------------------------------------------\n"
             "Verdict: %s%s%s%s\n"
             "Mode: %s\n"
             "Pinned: %s\n"
             "Calibrated Core: %d (Frequency: %d MHz, Ref Max: %d MHz, Global Max: %d MHz)\n"
             "Total Duration: %.1f ms (Warmup: %.1f ms)\n"
             "Chunk Counts: total=%d, accepted=%d, migrated=%d, foreign_cluster=%d,\n"
             "              preempted=%d, freq_changed=%d, unknown=%d\n"
             "Fit & Accuracy:\n"
             "  Rate: %llu iterations/ms\n"
             "  Fixed Overhead: 0.000 ms\n"
             "  R^2 Linearity: %.6f\n"
             "  RME95: +/-%.2f%%\n"
             "  Mean Error: %.2f%%\n"
             "  Spread: %.2f%%\n"
             "  Plateau Drift: %.2f%% (max: 2.50%%)\n\n",
             cpu_verdict, has_detail ? " (" : "", has_detail ? detail : "", has_detail ? ")" : "",
             cpu.mode.c_str(), cpu.pinned ? "YES" : "NO", cpu.core, cpu.freq_mhz, cpu.ref_max_mhz,
             s_device_global_max_freq_mhz.load(), cpu.duration_ms, cpu.warmup_ms, total_cpu_chunks,
             cpu.accepted_chunks, cpu.rejected_migrated, cpu.rejected_cluster,
             cpu.rejected_preempted, cpu.rejected_freq, cpu.rejected_unknown,
             (unsigned long long)cpu.iters_per_ms, cpu.r_squared, cpu.rme95_pct, cpu.mean_error_pct,
             cpu.spread_pct, cpu.plateau_drift_pct);
    report += buf;

    if (cpu.failure == nullptr && cpu.iters_per_ms > 0) {
        append_level_table(report, cpu.levels, 1);
    }
}

static void append_gpu_section(std::string& report, const GpuCalibrationResult& gpu,
                               const char* gpu_verdict, const std::string& gpu_failure) {
    int total_gpu_passes = gpu.accepted_samples + gpu.rejected_samples;
    const char* detail = gpu.failure ? gpu.failure : gpu_failure.c_str();
    const bool has_detail = detail[0] != '\0';

    char buf[1024];
    snprintf(buf, sizeof(buf),
             "--------------------------------------------------------------------------------\n"
             "                              GPU CALIBRATION\n"
             "--------------------------------------------------------------------------------\n"
             "Verdict: %s%s%s%s\n"
             "Timer: %s\n"
             "Frequency: %d MHz\n"
             "Total Duration: %.1f ms (Warmup: %.1f ms)\n"
             "Pass Counts: total=%d, accepted=%d, rejected=%d\n"
             "Fit & Accuracy:\n"
             "  Rate: %.4f iterations/ms\n"
             "  Fixed Overhead: %.4f ms\n"
             "  R^2 Linearity: %.6f\n"
             "  RME95: +/-%.2f%%\n"
             "  Mean Error: %.2f%%\n"
             "  Spread: %.2f%%\n"
             "  Plateau Drift: %.2f%% (max: 3.50%%)\n\n",
             gpu_verdict, has_detail ? " (" : "", has_detail ? detail : "", has_detail ? ")" : "",
             gpu.timer.c_str(), gpu.freq_mhz, gpu.duration_ms, gpu.warmup_ms, total_gpu_passes,
             gpu.accepted_samples, gpu.rejected_samples, gpu.iters_per_ms, gpu.fixed_ms,
             gpu.r_squared, gpu.rme95_pct, gpu.mean_error_pct, gpu.spread_pct,
             gpu.plateau_drift_pct);
    report += buf;

    if (gpu.failure == nullptr && gpu.iters_per_ms > 0.0) {
        append_level_table(report, gpu.levels, 2);
    }
}

// Appends the overall verdict: the run is tainted if it was flagged as such
// or a calibrated kind failed.
static void append_overall_status(std::string& report, CalibrationSet kinds,
                                  const char* cpu_verdict, const char* gpu_verdict) {
    const CalibrationSet gpu_kinds =
            calibration_bit(CALIBRATION_GLES) | calibration_bit(CALIBRATION_VULKAN);
    const bool is_tainted = s_run_tainted ||
            ((kinds & calibration_bit(CALIBRATION_CPU)) && strcmp(cpu_verdict, "FAIL") == 0) ||
            ((kinds & gpu_kinds) && strcmp(gpu_verdict, "FAIL") == 0);
    char buf[1024];
    snprintf(buf, sizeof(buf),
             "================================================================================\n"
             "OVERALL CALIBRATION STATUS: %s\n"
             "================================================================================\n",
             is_tainted ? "FAIL (TAINTED)" : "PASS (UNTAINTED)");
    report += buf;
}

static void write_report_file(const std::string& report, const std::string& path) {
    if (path.empty()) return;
    FILE* fp = fopen(path.c_str(), "w");
    if (fp) {
        fputs(report.c_str(), fp);
        fclose(fp);
        ALOGI("SwappyTest: Wrote calibration report to %s", path.c_str());
    } else {
        ALOGW("SwappyTest: Failed to open %s for calibration report", path.c_str());
    }
}

std::string calibration_report_filename(CalibrationKind kind) {
    return std::string("calibration_report_") + calibration_kind_name(kind) + ".txt";
}

// Writes the report as calibration_report.txt and under the name of each
// kind in kinds, to the primary output directory and, where different, to
// external storage.
static void write_report_files(const std::string& report, CalibrationSet kinds) {
    std::vector<std::string> names = {"calibration_report.txt"};
    for (int k = 0; k < CALIBRATION_KIND_COUNT; ++k) {
        const CalibrationKind kind = static_cast<CalibrationKind>(k);
        if (kinds & calibration_bit(kind)) names.push_back(calibration_report_filename(kind));
    }
    for (const std::string& name : names) {
        std::string primary_path;
        {
            std::lock_guard<std::mutex> lock(s_test_mutex);
            primary_path = output_path(name.c_str());
        }
        write_report_file(report, primary_path);
        const std::string ext_path = path_in(kExternalFilesDir, name.c_str());
        if (primary_path != ext_path) {
            write_report_file(report, ext_path);
        }
    }
}

void write_calibration_report(CalibrationSet kinds, const CpuCalibrationResult& cpu,
                              const GpuCalibrationResult& gpu, bool held) {
    const std::string cpu_failure = cpu_calibration_failure(cpu, held);
    const std::string gpu_failure = gpu_calibration_failure(gpu, held);
    const char* cpu_verdict = cpu_calibration_verdict(cpu, held);
    const char* gpu_verdict = gpu_calibration_verdict(gpu, held);

    std::string report;
    report.reserve(4096);
    append_report_header(report, kinds, gpu);
    if (kinds & calibration_bit(CALIBRATION_CPU)) {
        append_cpu_section(report, cpu, cpu_verdict, cpu_failure);
    }
    if (kinds & ~calibration_bit(CALIBRATION_CPU)) {
        append_gpu_section(report, gpu, gpu_verdict, gpu_failure);
    }
    append_overall_status(report, kinds, cpu_verdict, gpu_verdict);
    write_report_files(report, kinds);
}

} // namespace dvfs
