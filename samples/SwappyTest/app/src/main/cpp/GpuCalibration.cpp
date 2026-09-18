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

#include <stdio.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "CalibrationStats.h"
#include "Circle.h"
#include "CpuGpuFreq.h"
#include "DvfsBenchmark.h"
#include "DvfsBenchmarkInternal.h"
#include "GpuCalibrationTarget.h"
#include "Log.h"

namespace dvfs {

std::atomic<int> s_calibration_hold{CALIB_HOLD_NONE};

// How long the parked render thread sleeps between hold-state polls, and how
// often the calibration thread polls for the render thread's replies.
static constexpr long kCalibrationHoldParkMs = 5;
static constexpr long kCalibrationHoldPollMs = 1;
static constexpr uint64_t kRenderHoldAckTimeoutNs = 5000000000ULL;
static constexpr uint64_t kGpuCalibrationTimeoutNs = 10000000000ULL;

GpuCalibrationResult s_gpu_calibration_result;

bool acquire_render_hold(void) {
    s_calibration_hold.store(CALIB_HOLD_REQUESTED, std::memory_order_release);
    const uint64_t start_wait_ns = get_time_monotonic_ns();
    const uint64_t timeout_ns = kRenderHoldAckTimeoutNs;
    while (s_calibration_running) {
        int hold = s_calibration_hold.load(std::memory_order_acquire);
        if (hold == CALIB_HOLD_ACKED) {
            return true;
        }
        if ((get_time_monotonic_ns() - start_wait_ns) >= timeout_ns) {
            ALOGW("SwappyTest: Render hold ACK timed out after 5.0s");
            return false;
        }
        sleep_ms(kCalibrationHoldPollMs);
    }
    return false;
}

void release_render_hold(void) {
    s_calibration_hold.store(CALIB_HOLD_NONE, std::memory_order_release);
}

GpuCalibrationResult request_gpu_calibration(void) {
    s_calibration_hold.store(CALIB_HOLD_GPU_REQUESTED, std::memory_order_release);
    const uint64_t start_wait_ns = get_time_monotonic_ns();
    const uint64_t timeout_ns = kGpuCalibrationTimeoutNs;
    while (s_calibration_running) {
        int hold = s_calibration_hold.load(std::memory_order_acquire);
        if (hold == CALIB_HOLD_GPU_DONE) {
            std::lock_guard<std::mutex> lock(s_test_mutex);
            int expected = CALIB_HOLD_GPU_DONE;
            s_calibration_hold.compare_exchange_strong(expected, CALIB_HOLD_ACKED,
                                                       std::memory_order_acq_rel);
            return s_gpu_calibration_result;
        }
        if ((get_time_monotonic_ns() - start_wait_ns) >= timeout_ns) {
            ALOGW("SwappyTest: GPU calibration result timed out after 10.0s");
            std::lock_guard<std::mutex> lock(s_test_mutex);
            int expected = CALIB_HOLD_GPU_REQUESTED;
            s_calibration_hold.compare_exchange_strong(expected, CALIB_HOLD_ACKED,
                                                       std::memory_order_acq_rel);
            GpuCalibrationResult timeout_res{};
            timeout_res.failure = "gpu_timeout";
            return timeout_res;
        }
        sleep_ms(kCalibrationHoldPollMs);
    }
    {
        std::lock_guard<std::mutex> lock(s_test_mutex);
        int expected = CALIB_HOLD_GPU_REQUESTED;
        s_calibration_hold.compare_exchange_strong(expected, CALIB_HOLD_ACKED,
                                                   std::memory_order_acq_rel);
    }
    GpuCalibrationResult aborted_res{};
    aborted_res.failure = "aborted";
    return aborted_res;
}

std::string gpu_calibration_failure(const GpuCalibrationResult& gpu, bool held) {
    char reason[192];
    if (!held) return "calibration_hold_timeout";
    if (gpu.failure != nullptr || gpu.iters_per_ms <= 0.0) {
        snprintf(reason, sizeof(reason), "gpu_calibration_unavailable (%s)",
                 gpu.failure ? gpu.failure : "unknown");
        return reason;
    }
    if (gpu.r_squared < 0.970 || gpu.rme95_pct > 5.0) {
        snprintf(reason, sizeof(reason), "gpu_calibration_unstable (R2=%.4f, RME=%.1f%%)",
                 gpu.r_squared, gpu.rme95_pct);
        return reason;
    }
    if (gpu.fixed_ms < -1.0 || gpu.fixed_ms > 20.0) {
        snprintf(reason, sizeof(reason), "gpu_calibration_nonlinear (intercept %.2f ms)",
                 gpu.fixed_ms);
        return reason;
    }
    return "";
}

// The GPU calibration's load levels, and the samples its fit window spans
// (W = 4 full 4-level sweeps, 8 + 8).
static constexpr double kGpuLoadLevelsMs[4] = {5.0, 10.0, 17.0, 25.0};
static constexpr size_t kGpuWindowSamples = 16;
static constexpr size_t kGpuPerLevelWindow = 4;

// The pass size the GPU warmup converged on and its last pass duration.
struct GpuWarmup {
    bool ok = false;
    int32_t n_est = 256;
    double last_ms = 12.0;
};

struct GpuLevelSamples {
    std::vector<int32_t> iters;
    std::vector<double> meas_ms;
};

// The accepted passes of the GPU sweep and its per-level pass sizes.
struct GpuSweep {
    int32_t level_N[4];
    std::vector<double> meas_x_iters;
    std::vector<double> meas_y_ms;
    std::vector<int> accepted_gpu_khz;
    int max_seen_gpu_khz = -1;
    GpuLevelSamples levels[4];
    bool sizing_locked = false;
    size_t best_win_end = 0;
    double best_win_rme95 = 1e9;
};

// A completed sweep pass and the GPU frequency read during and after it.
struct GpuPass {
    samples::GpuPassTiming timing{};
    int32_t n = 0;
    int lvl_idx = 0;
    int freq_during = -1;
    int freq_after = -1;
};

// Runs the two driver warmup passes, then adapts the pass size toward ~12 ms
// passes, keeping the GPU continuously busy. On success the last warmup pass
// is still executing.
static GpuWarmup warm_up_gpu(samples::GpuCalibrationTarget& target, GpuCalibrationResult* res) {
    GpuWarmup warmup;
    // Kick off driver warmup Pass 0
    target.PreparePass(1);
    if (!target.SubmitPass(1)) {
        res->failure = "gpu_pass_failed";
        return warmup;
    }
    // Pre-record driver warmup Pass 1 while Pass 0 is executing on the GPU
    target.PreparePass(1);
    if (target.WaitPass().wall_ns <= 0 || !target.SubmitPass(1)) {
        res->failure = "gpu_pass_failed";
        return warmup;
    }

    // Pre-record first DVFS warmup pass while driver warmup Pass 1 is executing
    int32_t& N_est = warmup.n_est;
    target.PreparePass(N_est);
    const uint64_t warmup_start_ns = get_time_monotonic_ns();
    if (target.WaitPass().wall_ns <= 0 || !target.SubmitPass(N_est)) {
        res->failure = "gpu_pass_failed";
        return warmup;
    }

    // Warmup phase: keep GPU continuously busy while adapting N_est toward ~12 ms passes
    uint64_t accumulated_busy_ns = 0;
    while (s_calibration_running) {
        // Pre-record next warmup pass into the idle command buffer slot while GPU is busy
        target.PreparePass(N_est);

        samples::GpuPassTiming timing = target.WaitPass();
        if (timing.wall_ns <= 0) {
            res->failure = "gpu_pass_failed";
            return warmup;
        }
        // Immediately submit the pre-recorded pass before doing CPU math so GPU has zero idle gap
        if (!target.SubmitPass(N_est)) {
            res->failure = "gpu_pass_failed";
            return warmup;
        }

        // Now, while the newly submitted warmup pass is executing on the GPU, update N_est
        accumulated_busy_ns += timing.wall_ns;
        double d_ms = timing.wall_ns / 1e6;
        if (d_ms > 0.0) {
            warmup.last_ms = d_ms;
            double ratio = 12.0 / d_ms;
            ratio = std::clamp(ratio, 0.5, 4.0);
            N_est = std::clamp(static_cast<int32_t>(std::lround(N_est * ratio)), 2,
                               samples::Circle::kMaxGpuIterations);
        }

        if (accumulated_busy_ns >= 250000000ULL) { // >= 250 ms busy time
            res->warmup_converged = true;
            break;
        }
        if ((get_time_monotonic_ns() - warmup_start_ns) >= 1500000000ULL) {
            break;
        }
    }
    res->warmup_ms = (get_time_monotonic_ns() - warmup_start_ns) / 1e6;
    warmup.ok = true;
    return warmup;
}

// Maintain strictly increasing iteration counts across the 4 load levels,
// updated once per 4-level sweep from the 2-parameter OLS fit (rate, fixed_ms)
// so pipelined passes never alternate gross rates or invert Iters_N(17ms) < Iters_N(10ms).
static void init_gpu_level_sizes(const GpuWarmup& warmup, int32_t level_N[4]) {
    const int32_t N_est = warmup.n_est;
    double iters_1ms = (warmup.last_ms > 0.5) ? (N_est / warmup.last_ms) : (N_est / 12.0);
    if (iters_1ms < 0.2) iters_1ms = 1.0;

    for (int i = 0; i < 4; ++i) {
        int32_t n = std::clamp(static_cast<int32_t>(std::lround(kGpuLoadLevelsMs[i] * iters_1ms)),
                               2, samples::Circle::kMaxGpuIterations);
        if (i > 0 && n <= level_N[i - 1]) {
            n = std::min(level_N[i - 1] + std::max(1, level_N[i - 1] / 2),
                         samples::Circle::kMaxGpuIterations);
        }
        level_N[i] = n;
    }
}

// Drops the accepted passes recorded before the GPU reached a higher OPP.
static void discard_gpu_samples(GpuSweep* sweep, GpuCalibrationResult* res) {
    res->rejected_samples += static_cast<int>(sweep->meas_x_iters.size());
    res->accepted_samples = 0;
    sweep->meas_x_iters.clear();
    sweep->meas_y_ms.clear();
    sweep->accepted_gpu_khz.clear();
    sweep->sizing_locked = false;
    sweep->best_win_end = 0;
    sweep->best_win_rme95 = 1e9;
    for (int i = 0; i < 4; ++i) {
        sweep->levels[i].iters.clear();
        sweep->levels[i].meas_ms.clear();
    }
}

// Lock level_N[0..3] after the initial 4-level sizing sweep using the
// 2-parameter OLS fit (slope + intercept) so subsequent sweeps use
// constant, strictly monotonic Iters_N across {5, 10, 17, 25} ms.
static void lock_gpu_level_sizes(const SlidingWindowRegressionStats& win, GpuSweep* sweep) {
    int32_t* level_N = sweep->level_N;
    const double ols_rate = 1.0 / win.ols.slope;
    const double est_fixed_ms = std::clamp(win.ols.intercept, 0.0, 20.0);
    const double sub_fixed_ms = std::max(0.0, est_fixed_ms - 3.5);
    // If Level 0 hit a compositor/XR frame quantum floor (>= 6.5 ms with
    // elevated intercept), lift the lower shader targets above the floor.
    const double floor_boost_ms = (sweep->meas_y_ms[0] >= 6.5 && est_fixed_ms >= 2.2) ? 3.5 : 0.0;
    for (int i = 0; i < 4; ++i) {
        const double lifted_ms = kGpuLoadLevelsMs[i] + floor_boost_ms * (3 - i) / 3.0;
        const double net_target_ms =
                std::max({1.5 + 0.5 * i, lifted_ms * 0.75, lifted_ms - sub_fixed_ms});
        int32_t target_n = std::clamp(static_cast<int32_t>(std::lround(net_target_ms * ols_rate)),
                                      2, samples::Circle::kMaxGpuIterations);
        if (i > 0 && target_n <= level_N[i - 1]) {
            target_n = std::min(level_N[i - 1] + std::max(1, level_N[i - 1] / 3),
                                samples::Circle::kMaxGpuIterations);
        }
        level_N[i] = target_n;
    }
    sweep->sizing_locked = true;
}

// Refits the window after each full sweep: locks the level sizes, tracks the
// best window and returns true once the window converged.
static bool evaluate_gpu_window(GpuSweep* sweep) {
    const std::vector<double>& meas_x_iters = sweep->meas_x_iters;
    const size_t fit_w = std::min(meas_x_iters.size(), kGpuWindowSamples);
    SlidingWindowRegressionStats win =
            compute_sliding_window_regression(meas_x_iters, sweep->meas_y_ms, fit_w);
    if (!sweep->sizing_locked && win.ols.valid && win.ols.slope > 1e-9) {
        lock_gpu_level_sizes(win, sweep);
    }

    // Once >= 24 samples are collected (4 probe + 4 transition + 16 locked),
    // all 16 samples in the sliding window use the locked level_N[0..3].
    if (meas_x_iters.size() >= 24 && win.ols.valid && win.ols.slope > 1e-9) {
        const double score = win.ols.rme95_pct + (win.plateau_drift_pct > 5.0 ? 2.0 : 0.0);
        if (score < sweep->best_win_rme95) {
            sweep->best_win_rme95 = score;
            sweep->best_win_end = meas_x_iters.size();
        }
    }

    // Evaluate sliding-window convergence once we have >= 24 samples
    if (meas_x_iters.size() >= 24 && win.ols.valid && win.ols.r_squared >= 0.985 &&
        win.ols.rme95_pct <= 3.5 && win.ols.mean_error_pct <= 5.0 && win.plateau_drift_pct <= 3.5) {
        ALOGI("SwappyTest: GPU calibration converged early at %zu samples (win "
              "R2=%.5f, RME=%.2f%%, MeanErr=%.2f%%, drift=%.2f%%)",
              meas_x_iters.size(), win.ols.r_squared, win.ols.rme95_pct, win.ols.mean_error_pct,
              win.plateau_drift_pct);
        return true;
    } else if (meas_x_iters.size() >= 36 && win.ols.valid && win.ols.r_squared >= 0.972 &&
               win.ols.rme95_pct <= 4.8 && win.plateau_drift_pct <= 4.5) {
        ALOGI("SwappyTest: GPU calibration converged at %zu samples (win R2=%.5f, "
              "RME=%.2f%%, MeanErr=%.2f%%, drift=%.2f%%)",
              meas_x_iters.size(), win.ols.r_squared, win.ols.rme95_pct, win.ols.mean_error_pct,
              win.plateau_drift_pct);
        return true;
    }
    return false;
}

// Counts a rejected pass or adds it to the samples. Returns true once the
// sweep converged.
static bool record_gpu_pass(const GpuPass& prev, GpuSweep* sweep, GpuCalibrationResult* res) {
    if (prev.freq_during > sweep->max_seen_gpu_khz) {
        sweep->max_seen_gpu_khz = prev.freq_during;
        // If earlier samples were recorded at a lower GPU OPP (>5% below new peak),
        // discard pre-ramp samples so the window only contains peak-OPP passes.
        if (!sweep->accepted_gpu_khz.empty() &&
            sweep->accepted_gpu_khz.front() < static_cast<int>(sweep->max_seen_gpu_khz * 0.95)) {
            discard_gpu_samples(sweep, res);
        }
    }

    if (prev.timing.wall_ns <= 0) {
        res->rejected_samples++;
    } else if (prev.freq_during > 0 && prev.freq_after > 0 && prev.freq_during != prev.freq_after) {
        res->rejected_samples++;
    } else if (prev.freq_during > 0 && sweep->max_seen_gpu_khz > 0 &&
               prev.freq_during < static_cast<int>(sweep->max_seen_gpu_khz * 0.95)) {
        // Reject passes executed at a throttled / lower GPU OPP
        res->rejected_samples++;
    } else {
        const double wall_ms = prev.timing.wall_ns / 1e6;
        sweep->meas_x_iters.push_back(static_cast<double>(prev.n));
        sweep->meas_y_ms.push_back(wall_ms);
        res->accepted_samples++;
        if (prev.freq_during > 0) {
            sweep->accepted_gpu_khz.push_back(prev.freq_during);
        }

        sweep->levels[prev.lvl_idx].iters.push_back(prev.n);
        sweep->levels[prev.lvl_idx].meas_ms.push_back(wall_ms);

        if (sweep->meas_x_iters.size() >= 4 && (sweep->meas_x_iters.size() % 4 == 0)) {
            return evaluate_gpu_window(sweep);
        }
    }
    return false;
}

// Cycles the 4 load levels, always pre-recording the next pass while the
// current one executes, until the window converges or the 5.0 s budget runs
// out. False with res->failure set when the first sweep pass fails.
static bool sweep_gpu_levels(samples::GpuCalibrationTarget& target, GpuSweep* sweep,
                             GpuCalibrationResult* res) {
    // While the final warmup pass is still executing on the GPU, pre-record Sweep Pass 0
    int cur_lvl_idx = 0;
    int32_t cur_N = sweep->level_N[0];
    target.PreparePass(cur_N);

    const uint64_t sweep_start_ns = get_time_monotonic_ns();
    const uint64_t sweep_budget_ns = 5000000000ULL; // 5.0 s maximum budget

    // Drain final warmup pass and immediately submit Sweep Pass 0 with zero idle gap
    if (target.WaitPass().wall_ns <= 0 || !target.SubmitPass(cur_N)) {
        res->failure = "gpu_pass_failed";
        return false;
    }
    sweep->max_seen_gpu_khz = read_gpu_freq_khz();

    int sweep_step = 0;
    bool has_prev_pass = false;
    GpuPass prev;
    while (s_calibration_running) {
        // === CPU WORK OVERLAPPED WITH ACTIVE GPU EXECUTION OF PASS `sweep_step` ===
        const uint64_t elapsed_sweep_ns = get_time_monotonic_ns() - sweep_start_ns;
        const int gpu_freq_during = read_gpu_freq_khz();

        bool should_stop = has_prev_pass && record_gpu_pass(prev, sweep, res);

        if (elapsed_sweep_ns >= sweep_budget_ns) {
            ALOGI("SwappyTest: GPU calibration reached 5.0s budget (%zu accepted samples)",
                  sweep->meas_x_iters.size());
            should_stop = true;
        }

        const int next_lvl_idx = (sweep_step + 1) % 4;
        const int32_t next_N = sweep->level_N[next_lvl_idx];

        if (!should_stop) {
            // Pre-record Pass `sweep_step + 1` into the idle command buffer slot
            // while Pass `sweep_step` is still executing on the GPU!
            target.PreparePass(next_N);
        }

        // Wait for Pass `sweep_step` to complete on the GPU
        samples::GpuPassTiming timing = target.WaitPass();
        if (should_stop || !s_calibration_running) {
            break;
        }

        // Immediately submit Pass `sweep_step + 1` with near-zero turnaround
        if (!target.SubmitPass(next_N)) {
            res->rejected_samples++;
            break;
        }
        const int gpu_freq_after = read_gpu_freq_khz();

        prev = GpuPass{timing, cur_N, cur_lvl_idx, gpu_freq_during, gpu_freq_after};
        has_prev_pass = true;

        cur_N = next_N;
        cur_lvl_idx = next_lvl_idx;
        sweep_step++;
    }
    return true;
}

// Truncates the samples to the best-scoring window and sets the median GPU
// frequency of the last window.
static void trim_gpu_sweep(GpuSweep* sweep, GpuCalibrationResult* res) {
    const size_t best_win_end = sweep->best_win_end;
    if (best_win_end >= kGpuWindowSamples && best_win_end < sweep->meas_x_iters.size()) {
        sweep->meas_x_iters.resize(best_win_end);
        sweep->meas_y_ms.resize(best_win_end);
        const size_t per_lvl_end = best_win_end / 4;
        for (int i = 0; i < 4; ++i) {
            if (sweep->levels[i].iters.size() > per_lvl_end) {
                sweep->levels[i].iters.resize(per_lvl_end);
                sweep->levels[i].meas_ms.resize(per_lvl_end);
            }
        }
    }

    const std::vector<int>& accepted_gpu_khz = sweep->accepted_gpu_khz;
    if (!accepted_gpu_khz.empty()) {
        const size_t khz_start = (accepted_gpu_khz.size() > kGpuWindowSamples)
                ? (accepted_gpu_khz.size() - kGpuWindowSamples)
                : 0;
        std::vector<int> win_khz(accepted_gpu_khz.begin() + khz_start, accepted_gpu_khz.end());
        std::sort(win_khz.begin(), win_khz.end());
        res->freq_mhz = win_khz[win_khz.size() / 2] / 1000;
    }
}

// The indices of the last kGpuPerLevelWindow samples of a level. If >= 3
// samples exist for this level, exclude at most 1 isolated OS/compositor spike
// exceeding 3x RME95 when computing the level summary stats.
static std::vector<size_t> gpu_level_window(const GpuLevelSamples& rec,
                                            const LinearRegressionStats& stats) {
    const size_t total_samples = rec.meas_ms.size();
    const size_t win_count = std::min(total_samples, kGpuPerLevelWindow);
    const size_t start_k = total_samples - win_count;

    std::vector<size_t> valid_indices;
    for (size_t k = start_k; k < total_samples; ++k) {
        valid_indices.push_back(k);
    }
    if (valid_indices.size() >= 3) {
        size_t worst_idx = valid_indices[0];
        double worst_err = -1.0;
        for (size_t k : valid_indices) {
            double exp_ms = stats.intercept + stats.slope * rec.iters[k];
            double err_pct =
                    (exp_ms > 1e-6) ? (std::abs(rec.meas_ms[k] - exp_ms) / exp_ms * 100.0) : 0.0;
            if (err_pct > worst_err) {
                worst_err = err_pct;
                worst_idx = k;
            }
        }
        if (worst_err > std::max(5.0, 3.0 * stats.rme95_pct)) {
            valid_indices.erase(std::remove(valid_indices.begin(), valid_indices.end(), worst_idx),
                                valid_indices.end());
        }
    }
    return valid_indices;
}

// Populate per-level statistics and compute spread strictly over the sliding window (last 4 passes
// per level)
static void summarize_gpu_levels(const GpuSweep& sweep, const LinearRegressionStats& stats,
                                 GpuCalibrationResult* res) {
    res->levels.clear();
    std::vector<double> level_rates;
    for (int i = 0; i < 4; ++i) {
        const auto& rec = sweep.levels[i];
        if (rec.meas_ms.empty()) continue;
        const std::vector<size_t> valid_indices = gpu_level_window(rec, stats);

        const size_t eff_count = valid_indices.size();
        CalibrationLevelStat lvl{};
        lvl.target_ms = kGpuLoadLevelsMs[i];
        lvl.samples = static_cast<int>(eff_count);
        lvl.iters_N = static_cast<uint64_t>(rec.iters[valid_indices.back()]);

        double sum_meas = 0.0;
        for (size_t k : valid_indices) sum_meas += rec.meas_ms[k];
        lvl.mean_meas_ms = sum_meas / eff_count;

        double sum_sq_diff = 0.0;
        for (size_t k : valid_indices) {
            double diff = rec.meas_ms[k] - lvl.mean_meas_ms;
            sum_sq_diff += diff * diff;
        }
        lvl.std_ms = (eff_count > 1) ? std::sqrt(sum_sq_diff / (eff_count - 1)) : 0.0;

        double sum_err = 0.0, max_err = 0.0;
        for (size_t k : valid_indices) {
            double expected_ms = stats.intercept + stats.slope * rec.iters[k];
            double err_pct = (expected_ms > 1e-12)
                    ? (std::abs(rec.meas_ms[k] - expected_ms) / expected_ms * 100.0)
                    : 0.0;
            sum_err += err_pct;
            if (err_pct > max_err) max_err = err_pct;
        }
        lvl.mean_err_pct = sum_err / eff_count;
        lvl.max_err_pct = max_err;
        const double net_meas_ms = std::max(0.1, lvl.mean_meas_ms - stats.intercept);
        lvl.level_rate = (net_meas_ms > 1e-12) ? (lvl.iters_N / net_meas_ms) : 0.0;
        if (lvl.level_rate > 0.0) {
            level_rates.push_back(lvl.level_rate);
        }
        res->levels.push_back(lvl);
    }

    if (level_rates.size() >= 2) {
        double min_r = *std::min_element(level_rates.begin(), level_rates.end());
        double max_r = *std::max_element(level_rates.begin(), level_rates.end());
        double avg_r =
                std::accumulate(level_rates.begin(), level_rates.end(), 0.0) / level_rates.size();
        res->spread_pct = (avg_r > 1e-12) ? ((max_r - min_r) / avg_r * 100.0) : 0.0;
    }
}

// Render-thread execution of offscreen GPU calibration with zero-idle-gap pipelining.
// Overlaps CPU command recording, sysfs pread, and sliding-window OLS math with active
// GPU execution so the GPU never goes idle between passes.
static GpuCalibrationResult calibrate_gpu(samples::GpuCalibrationTarget& target) {
    GpuCalibrationResult res{};
    const uint64_t start_ns = get_time_monotonic_ns();
    res.pixels = target.PixelCount();
    res.timer = target.WallTimerName();
    int cur_gpu_khz = read_gpu_freq_khz();
    res.freq_mhz = khz_to_mhz(cur_gpu_khz);

    const GpuWarmup warmup = warm_up_gpu(target, &res);
    if (!warmup.ok) {
        return res;
    }
    if (!s_calibration_running) {
        target.WaitPass();
        res.failure = "aborted";
        return res;
    }

    GpuSweep sweep;
    init_gpu_level_sizes(warmup, sweep.level_N);
    if (!sweep_gpu_levels(target, &sweep, &res)) {
        return res;
    }

    res.duration_ms = (get_time_monotonic_ns() - start_ns) / 1e6;
    trim_gpu_sweep(&sweep, &res);

    if (sweep.meas_x_iters.size() < 8) {
        res.failure = "insufficient_samples";
        return res;
    }

    SlidingWindowRegressionStats win =
            compute_sliding_window_regression(sweep.meas_x_iters, sweep.meas_y_ms,
                                              kGpuWindowSamples);
    const LinearRegressionStats& stats = win.ols;
    if (!stats.valid || stats.slope <= 1e-12) {
        res.failure = "gpu_fit_failed";
        return res;
    }

    res.r_squared = stats.r_squared;
    res.rme95_pct = stats.rme95_pct;
    res.mean_error_pct = stats.mean_error_pct;
    res.plateau_drift_pct = win.plateau_drift_pct;
    // Slope = ms per shader iteration -> Rate = 1 / slope (iters/ms)
    res.iters_per_ms = 1.0 / stats.slope;
    res.fixed_ms = stats.intercept;

    summarize_gpu_levels(sweep, stats, &res);

    ALOGI("SwappyTest: GPU calibration finished: iters_per_ms=%.3f, fixed_ms=%.3f (R2=%.5f, "
          "RME=%.2f%%, MeanErr=%.2f%%, spread=%.2f%%, drift=%.2f%%, samples=%d)",
          res.iters_per_ms, res.fixed_ms, res.r_squared, res.rme95_pct, res.mean_error_pct,
          res.spread_pct, res.plateau_drift_pct, res.accepted_samples);
    return res;
}

} // namespace dvfs

using dvfs::CALIB_HOLD_ACKED;
using dvfs::CALIB_HOLD_GPU_DONE;
using dvfs::CALIB_HOLD_GPU_REQUESTED;
using dvfs::CALIB_HOLD_NONE;
using dvfs::CALIB_HOLD_REQUESTED;
using dvfs::calibrate_gpu;
using dvfs::GpuCalibrationResult;
using dvfs::kCalibrationHoldParkMs;
using dvfs::s_calibration_hold;
using dvfs::s_gpu_calibration_result;
using dvfs::s_test_mutex;
using dvfs::sleep_ms;

// CalibrationHoldAction implementation
CalibrationHoldAction poll_calibration_hold(void) {
    int hold = s_calibration_hold.load(std::memory_order_acquire);
    if (hold == CALIB_HOLD_NONE) {
        return CalibrationHoldAction::kNone;
    }
    if (hold == CALIB_HOLD_REQUESTED) {
        return CalibrationHoldAction::kDrainAndAcknowledge;
    }
    if (hold == CALIB_HOLD_GPU_REQUESTED) {
        return CalibrationHoldAction::kRunGpuCalibration;
    }
    // CALIB_HOLD_ACKED or CALIB_HOLD_GPU_DONE: park render thread briefly
    sleep_ms(kCalibrationHoldParkMs);
    return CalibrationHoldAction::kIdle;
}

void acknowledge_calibration_hold(void) {
    int expected = CALIB_HOLD_REQUESTED;
    s_calibration_hold.compare_exchange_strong(expected, CALIB_HOLD_ACKED,
                                               std::memory_order_acq_rel);
}

void run_gpu_calibration(samples::GpuCalibrationTarget& target) {
    GpuCalibrationResult res = calibrate_gpu(target);
    std::lock_guard<std::mutex> lock(s_test_mutex);
    int expected = CALIB_HOLD_GPU_REQUESTED;
    if (s_calibration_hold.compare_exchange_strong(expected, CALIB_HOLD_GPU_DONE,
                                                   std::memory_order_acq_rel)) {
        s_gpu_calibration_result = std::move(res);
    }
}
