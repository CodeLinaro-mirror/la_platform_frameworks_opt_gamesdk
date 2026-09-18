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

#pragma once

// Calibration of the CPU and GPU workload rates: the chunked CPU sweep, the
// GPU sweep the render thread runs while it holds for calibration, the
// per-kind calibration caches and the calibration report.

#include <stdint.h>

#include <atomic>
#include <string>
#include <vector>

#include "CalibrationKind.h"

namespace dvfs {

// Hold state machine for Swappy-independent calibration window
enum CalibrationHoldState : int {
    CALIB_HOLD_NONE = 0,
    CALIB_HOLD_REQUESTED,
    CALIB_HOLD_ACKED,
    CALIB_HOLD_GPU_REQUESTED,
    CALIB_HOLD_GPU_DONE,
};
extern std::atomic<int> s_calibration_hold;

struct CalibrationLevelStat {
    double target_ms = 0.0;
    uint64_t iters_N = 0;
    double mean_meas_ms = 0.0;
    double std_ms = 0.0;
    double mean_err_pct = 0.0;
    double max_err_pct = 0.0;
    double level_rate = 0.0;
    int samples = 0;
};

struct GpuCalibrationResult {
    double iters_per_ms = 0.0;
    double fixed_ms = 0.0;
    double r_squared = 0.0;
    double rme95_pct = 0.0;
    double mean_error_pct = 0.0;
    double spread_pct = 0.0;
    double plateau_drift_pct = 0.0;
    int accepted_samples = 0;
    int rejected_samples = 0;
    int64_t pixels = 0;
    std::string timer = "none";
    int freq_mhz = -1;
    double warmup_ms = 0.0;
    double duration_ms = 0.0;
    bool warmup_converged = false;
    const char* failure = nullptr;
    std::vector<CalibrationLevelStat> levels;
};

struct CpuCalibrationResult {
    uint64_t iters_per_ms = 0;
    std::string mode = "at_max";
    int core = -1;
    int freq_mhz = -1;
    int ref_max_mhz = -1;
    int accepted_chunks = 0;
    int rejected_migrated = 0;
    int rejected_freq = 0;
    int rejected_preempted = 0;
    int rejected_cluster = 0;
    int rejected_unknown = 0;
    double r_squared = 0.0;
    double rme95_pct = 0.0;
    double mean_error_pct = 0.0;
    double spread_pct = 0.0;
    double plateau_drift_pct = 0.0;
    double warmup_ms = 0.0;
    double duration_ms = 0.0;
    bool pinned = false;
    bool reached_max_freq = false;
    const char* failure = nullptr;
    std::vector<CalibrationLevelStat> levels;
};

// The latest results, reported in the run record.
extern GpuCalibrationResult s_gpu_calibration_result;
extern CpuCalibrationResult s_cpu_calibration_result;

// What the CPU and GPU calibration caches store for scenario runs.
struct CpuCalibrationCache {
    uint64_t iters_per_ms = 0;
    int core = -1;
    int freq_mhz = -1;
    std::string mode = "at_max";
    double r_squared = 0.0;
    double rme95_pct = 0.0;
    double mean_error_pct = 0.0;
    int accepted_chunks = 0;
};

struct GpuCalibrationCache {
    double iters_per_ms = 0.0;
    double fixed_ms = 0.0;
    int64_t pixels = 0;
    std::string timer = "none";
    double r_squared = 0.0;
    double rme95_pct = 0.0;
    double mean_error_pct = 0.0;
    int accepted_samples = 0;
};

// Load the CPU cache, or the cache of GPU kind, into *cpu or *gpu. Valid only
// when it was stored by this cache version on this build and, for a GPU
// kind, for a surface of expected_pixels (unchecked when <= 0). An obsolete
// cache counts as missing.
CalibrationStatus load_cpu_calibration_cache(CpuCalibrationCache* cpu);
CalibrationStatus load_gpu_calibration_cache(CalibrationKind kind, int64_t expected_pixels,
                                             GpuCalibrationCache* gpu);
void save_cpu_calibration_cache(const CpuCalibrationResult& cpu, int global_max_mhz);
void save_gpu_calibration_cache(CalibrationKind kind, const GpuCalibrationResult& gpu,
                                int64_t surface_pixels);

// The report name of kind, such as "calibration_report_cpu.txt".
std::string calibration_report_filename(CalibrationKind kind);

// Writes the calibration report of the calibrated kinds: the CPU section
// when kinds has the CPU kind, the GPU section when it has a GPU kind. It is
// written as calibration_report.txt and under the name of each kind.
void write_calibration_report(CalibrationSet kinds, const CpuCalibrationResult& cpu,
                              const GpuCalibrationResult& gpu, bool held);

CpuCalibrationResult run_chunked_cpu_calibration(void);

// The calibration thread's side of the render-thread hold handshake.
bool acquire_render_hold(void);
void release_render_hold(void);
GpuCalibrationResult request_gpu_calibration(void);

// Why a calibration result must not be stored, or "" when it may: a held
// false means the render thread never parked for the calibration.
std::string cpu_calibration_failure(const CpuCalibrationResult& cpu, bool held);
std::string gpu_calibration_failure(const GpuCalibrationResult& gpu, bool held);

} // namespace dvfs
