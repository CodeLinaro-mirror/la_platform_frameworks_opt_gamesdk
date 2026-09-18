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

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <string>
#include <vector>

#include "CalibrationStats.h"
#include "CpuGpuFreq.h"
#include "DvfsBenchmarkInternal.h"
#include "Log.h"

namespace dvfs {

CpuCalibrationResult s_cpu_calibration_result;

// RAII CPU affinity management pinning the calling thread to the prime/reference cluster
class ScopedCpuAffinity {
public:
    explicit ScopedCpuAffinity(const std::vector<int>& target_cpus) {
        if (target_cpus.empty()) return;
        CPU_ZERO(&mSavedMask);
        if (sched_getaffinity(0, sizeof(cpu_set_t), &mSavedMask) == 0) {
            mHasSavedMask = true;
            cpu_set_t targetMask;
            CPU_ZERO(&targetMask);
            for (int cpu : target_cpus) {
                if (cpu >= 0 && cpu < CPU_SETSIZE) {
                    CPU_SET(cpu, &targetMask);
                }
            }
            if (sched_setaffinity(0, sizeof(cpu_set_t), &targetMask) == 0) {
                mPinned = true;
            } else {
                ALOGW("SwappyTest: sched_setaffinity failed; running unpinned");
            }
        }
    }

    ~ScopedCpuAffinity() {
        if (mPinned && mHasSavedMask) {
            sched_setaffinity(0, sizeof(cpu_set_t), &mSavedMask);
        }
    }

    bool isPinned() const {
        return mPinned;
    }

private:
    cpu_set_t mSavedMask;
    bool mHasSavedMask = false;
    bool mPinned = false;
};

struct CpuChunk {
    int begin_core = -1, end_core = -1;
    int begin_khz = -1, end_khz = -1;
    uint64_t iterations = 0;
    uint64_t wall_ns = 0;
    uint64_t on_cpu_ns = 0;
};

static CpuChunk run_cpu_chunk(uint64_t iterations) {
    CpuChunk chunk{};
    chunk.iterations = iterations;
    chunk.begin_core = sched_getcpu();
    chunk.begin_khz = read_cpu_freq_khz(chunk.begin_core);
    const uint64_t on_cpu_start_ns = get_thread_cpu_time_ns();
    const uint64_t start_ns = get_time_monotonic_ns();
    execute_cpu_workload(iterations);
    const uint64_t end_ns = get_time_monotonic_ns();
    chunk.on_cpu_ns = get_thread_cpu_time_ns() - on_cpu_start_ns;
    chunk.end_core = sched_getcpu();
    chunk.end_khz = read_cpu_freq_khz(chunk.end_core);
    chunk.wall_ns = end_ns - start_ns;
    return chunk;
}

enum class CpuVerdict {
    kAtMax,
    kBelowMax,
    kMigrated,
    kForeignCluster,
    kPreempted,
    kFreqChanged,
    kFreqUnknown
};

static CpuVerdict classify_cpu_chunk(const CpuChunk& chunk, const std::vector<int>& ref_cpus,
                                     int ref_max_khz) {
    if (chunk.begin_core != chunk.end_core) {
        return CpuVerdict::kMigrated;
    }
    if (!ref_cpus.empty()) {
        bool in_ref = false;
        for (int c : ref_cpus) {
            if (c == chunk.begin_core) {
                in_ref = true;
                break;
            }
        }
        if (!in_ref) return CpuVerdict::kForeignCluster;
    }
    if (chunk.wall_ns > 0 && chunk.on_cpu_ns < (uint64_t)(0.99 * chunk.wall_ns)) {
        return CpuVerdict::kPreempted;
    }
    if (chunk.begin_khz <= 0 || chunk.end_khz <= 0) {
        return CpuVerdict::kFreqUnknown;
    }
    if (chunk.begin_khz != chunk.end_khz) {
        return CpuVerdict::kFreqChanged;
    }
    if (ref_max_khz > 0 && chunk.begin_khz >= static_cast<int>(ref_max_khz * 0.985)) {
        return CpuVerdict::kAtMax;
    }
    return CpuVerdict::kBelowMax;
}

// The CPU calibration's load levels, the samples its fit window spans (four
// full sweeps), and the samples per level its per-level breakdown spans.
static constexpr double kCpuLoadLevelsMs[4] = {1.0, 2.0, 4.0, 7.0};
static constexpr size_t kCpuWindowSamples = 16;
static constexpr size_t kCpuPerLevelWindow = 4;

// The cluster the CPU calibration measures: its max frequency and its CPUs.
struct CpuReference {
    int max_khz = -1;
    std::vector<int> cpus;
};

struct CpuLevelSamples {
    std::vector<uint64_t> iters;
    std::vector<double> meas_ms;
};

// The accepted chunks of the CPU sweep, and the sweep's iterations per ms
// estimate.
struct CpuSweep {
    std::vector<double> meas_x_iters;
    std::vector<double> meas_y_ms;
    std::map<int, int> core_counts;
    std::vector<int> accepted_khz;
    std::vector<double> recent_rates;
    CpuLevelSamples levels[4];
    uint64_t iters_1ms = 0;
    bool seen_at_max = false;
};

// The highest-frequency cluster the process may run on.
static CpuReference find_cpu_reference(CpuCalibrationResult* res) {
    const int global_max_khz = find_device_global_max_freq_khz();
    CpuReference ref;
    ref.max_khz = global_max_khz;

    // Respect the process's initial cpuset mask (e.g., Android Go boards where
    // foreground app threads are restricted by cpuset to LITTLE cores even if
    // sched_setaffinity returns 0).
    cpu_set_t initial_mask;
    CPU_ZERO(&initial_mask);
    if (sched_getaffinity(0, sizeof(initial_mask), &initial_mask) == 0) {
        int mask_max_khz = -1;
        for (int c = 0; c < kMaxCpus; ++c) {
            if (CPU_ISSET(c, &initial_mask)) {
                int khz = read_cpuinfo_max_freq_khz(c);
                if (khz > mask_max_khz) mask_max_khz = khz;
            }
        }
        if (mask_max_khz > 0) {
            ref.max_khz = mask_max_khz;
        }
    }
    res->ref_max_mhz = khz_to_mhz(ref.max_khz);

    if (ref.max_khz > 0) {
        for (int c = 0; c < kMaxCpus; ++c) {
            if (read_cpuinfo_max_freq_khz(c) == ref.max_khz) {
                ref.cpus.push_back(c);
            }
        }
    }
    return ref;
}

// Moves the reference to the cluster of active_core when the scheduler keeps
// the thread off the reference cluster.
static void reanchor_to_active_cluster(int active_core, CpuReference* ref,
                                       CpuCalibrationResult* res) {
    if (active_core < 0 || active_core >= kMaxCpus) return;
    int actual_max_khz = read_cpuinfo_max_freq_khz(active_core);
    if (actual_max_khz <= 0 || actual_max_khz == ref->max_khz) return;
    ref->max_khz = actual_max_khz;
    res->ref_max_mhz = ref->max_khz / 1000;
    ref->cpus.clear();
    for (int c = 0; c < kMaxCpus; ++c) {
        if (read_cpuinfo_max_freq_khz(c) == ref->max_khz) {
            ref->cpus.push_back(c);
        }
    }
    cpu_set_t fallback_mask;
    CPU_ZERO(&fallback_mask);
    for (int c : ref->cpus) CPU_SET(c, &fallback_mask);
    if (sched_setaffinity(0, sizeof(fallback_mask), &fallback_mask) == 0 &&
        sched_getcpu() == active_core) {
        res->pinned = true;
    }
    ALOGI("SwappyTest: Re-anchored CPU calibration cluster to core %d (ref_max=%d MHz, %zu "
          "cores)",
          active_core, res->ref_max_mhz, ref->cpus.size());
}

// Warmup phase: ramp cpufreq to peak frequency on reference core. Returns the
// chunk size sized toward 2 ms chunks.
static uint64_t warm_up_cpu(CpuReference* ref, CpuCalibrationResult* res) {
    uint64_t warmup_iters = 200000ULL;
    int consecutive_at_max = 0;
    int warmup_foreign_count = 0;
    const uint64_t warmup_start_ns = get_time_monotonic_ns();
    const uint64_t warmup_min_ns = 100000000ULL;  // 100 ms
    const uint64_t warmup_max_ns = 1500000000ULL; // 1500 ms

    while (s_calibration_running) {
        const uint64_t elapsed_ns = get_time_monotonic_ns() - warmup_start_ns;
        CpuChunk chunk = run_cpu_chunk(warmup_iters);
        CpuVerdict verdict = classify_cpu_chunk(chunk, ref->cpus, ref->max_khz);

        if (verdict == CpuVerdict::kForeignCluster || verdict == CpuVerdict::kMigrated) {
            warmup_foreign_count++;
            if (warmup_foreign_count >= 6 && chunk.begin_core == chunk.end_core) {
                reanchor_to_active_cluster(chunk.begin_core, ref, res);
                warmup_foreign_count = 0;
                verdict = classify_cpu_chunk(chunk, ref->cpus, ref->max_khz);
            }
        }

        if (verdict == CpuVerdict::kAtMax) {
            consecutive_at_max++;
        } else {
            consecutive_at_max = 0;
        }

        if (chunk.wall_ns > 0) {
            double ratio = 2000000.0 / static_cast<double>(chunk.wall_ns);
            ratio = std::clamp(ratio, 0.5, 2.0);
            warmup_iters = std::clamp(static_cast<uint64_t>(warmup_iters * ratio),
                                      static_cast<uint64_t>(10000ULL),
                                      static_cast<uint64_t>(100000000ULL));
        }

        if (elapsed_ns >= warmup_min_ns && consecutive_at_max >= 8) {
            res->reached_max_freq = true;
            break;
        }
        if (elapsed_ns >= warmup_max_ns) {
            res->reached_max_freq = (consecutive_at_max >= 4);
            break;
        }
    }
    res->warmup_ms = (get_time_monotonic_ns() - warmup_start_ns) / 1e6;
    return warmup_iters;
}

// Drops the accepted chunks, counting them as frequency rejections.
static void discard_cpu_samples(CpuSweep* sweep, CpuCalibrationResult* res) {
    res->rejected_freq += static_cast<int>(sweep->meas_x_iters.size());
    res->accepted_chunks = 0;
    sweep->meas_x_iters.clear();
    sweep->meas_y_ms.clear();
    sweep->core_counts.clear();
    sweep->accepted_khz.clear();
    sweep->recent_rates.clear();
    for (int i = 0; i < 4; ++i) {
        sweep->levels[i].iters.clear();
        sweep->levels[i].meas_ms.clear();
    }
}

// Once peak OPP is reached, reject transient dips to penultimate OPPs
// so the regression window never mixes two different CPU frequencies.
// False when the chunk is rejected.
static bool track_cpu_plateau(const CpuChunk& chunk, CpuVerdict verdict, CpuSweep* sweep,
                              CpuCalibrationResult* res) {
    if (verdict == CpuVerdict::kAtMax && !sweep->seen_at_max) {
        sweep->seen_at_max = true;
        res->reached_max_freq = true;
        if (!sweep->meas_x_iters.empty()) {
            discard_cpu_samples(sweep, res);
        }
    } else if (verdict == CpuVerdict::kBelowMax && sweep->seen_at_max) {
        res->rejected_freq++;
        return false;
    } else if (!sweep->accepted_khz.empty() && chunk.begin_khz > 0) {
        const int prev_khz = sweep->accepted_khz.back();
        if (std::abs(chunk.begin_khz - prev_khz) > static_cast<int>(prev_khz * 0.015)) {
            if (chunk.begin_khz > prev_khz) {
                // Frequency stepped up to a higher plateau; reset window to higher OPP
                discard_cpu_samples(sweep, res);
            } else {
                res->rejected_freq++;
                return false;
            }
        }
    }
    return true;
}

// Adds a chunk at the plateau frequency to the samples. False when it is
// rejected as a stall.
static bool accept_cpu_chunk(const CpuChunk& chunk, double target_ms, int lvl_idx, CpuSweep* sweep,
                             CpuCalibrationResult* res) {
    double wall_ms = static_cast<double>(chunk.wall_ns) / 1e6;
    double current_rate = (wall_ms > 0.0) ? (chunk.iterations / wall_ms) : 0.0;

    // Reject single-chunk ISR / hypervisor stalls (>5% rate drop from rolling median)
    // that do not trigger a kernel thread context switch (on_cpu_ns >= 0.99 * wall_ns).
    const std::vector<double>& recent_rates = sweep->recent_rates;
    if (recent_rates.size() >= 4 && current_rate > 0.0) {
        const size_t r_start = (recent_rates.size() > 8) ? (recent_rates.size() - 8) : 0;
        std::vector<double> r_win(recent_rates.begin() + r_start, recent_rates.end());
        std::sort(r_win.begin(), r_win.end());
        const double med_rate = r_win[r_win.size() / 2];
        if (med_rate > 0.0 && (current_rate < 0.95 * med_rate || current_rate > 1.05 * med_rate)) {
            res->rejected_preempted++;
            return false;
        }
    }

    sweep->meas_x_iters.push_back(static_cast<double>(chunk.iterations));
    sweep->meas_y_ms.push_back(wall_ms);
    res->accepted_chunks++;
    sweep->core_counts[chunk.begin_core]++;
    sweep->accepted_khz.push_back(chunk.begin_khz);
    if (current_rate > 0.0) {
        sweep->recent_rates.push_back(current_rate);
    }

    sweep->levels[lvl_idx].iters.push_back(chunk.iterations);
    sweep->levels[lvl_idx].meas_ms.push_back(wall_ms);

    // Refine 1ms iteration estimate
    if (target_ms > 0.0 && current_rate > 0.0) {
        sweep->iters_1ms = static_cast<uint64_t>(std::lround(current_rate));
    }
    return true;
}

// Counts a rejected sweep chunk or adds it to the samples. False when the
// chunk is dropped before the convergence check.
static bool record_cpu_chunk(const CpuChunk& chunk, double target_ms, int lvl_idx,
                             CpuReference* ref, CpuSweep* sweep, CpuCalibrationResult* res) {
    CpuVerdict verdict = classify_cpu_chunk(chunk, ref->cpus, ref->max_khz);

    if ((verdict == CpuVerdict::kForeignCluster || verdict == CpuVerdict::kMigrated) &&
        res->accepted_chunks == 0 && (res->rejected_cluster + res->rejected_migrated) >= 6 &&
        chunk.begin_core == chunk.end_core) {
        reanchor_to_active_cluster(chunk.begin_core, ref, res);
        verdict = classify_cpu_chunk(chunk, ref->cpus, ref->max_khz);
    }

    if (verdict == CpuVerdict::kMigrated) {
        res->rejected_migrated++;
    } else if (verdict == CpuVerdict::kForeignCluster) {
        res->rejected_cluster++;
    } else if (verdict == CpuVerdict::kPreempted) {
        res->rejected_preempted++;
    } else if (verdict == CpuVerdict::kFreqChanged) {
        res->rejected_freq++;
    } else if (verdict == CpuVerdict::kFreqUnknown) {
        res->rejected_unknown++;
    } else if (verdict == CpuVerdict::kAtMax || verdict == CpuVerdict::kBelowMax) {
        if (!track_cpu_plateau(chunk, verdict, sweep, res)) return false;
        return accept_cpu_chunk(chunk, target_ms, lvl_idx, sweep, res);
    }
    return false;
}

// Cycles the 4 load levels until the sliding window converges or the 5.0 s
// budget runs out.
static void sweep_cpu_levels(CpuReference* ref, CpuSweep* sweep, CpuCalibrationResult* res) {
    const uint64_t sweep_start_ns = get_time_monotonic_ns();
    const uint64_t sweep_budget_ns = 5000000000ULL; // 5.0 s maximum budget

    int level_idx = 0;
    while (s_calibration_running) {
        const int cur_lvl_idx = level_idx % 4;
        const double target_ms = kCpuLoadLevelsMs[cur_lvl_idx];
        level_idx++;

        uint64_t iters = static_cast<uint64_t>(std::lround(target_ms * sweep->iters_1ms));
        if (iters < 1000ULL) iters = 1000ULL;

        CpuChunk chunk = run_cpu_chunk(iters);
        const std::vector<double>& meas_x_iters = sweep->meas_x_iters;
        if (record_cpu_chunk(chunk, target_ms, cur_lvl_idx, ref, sweep, res)) {
            // Check for early convergence once we have at least 16 accepted samples (W = 4 full
            // sweeps)
            if (meas_x_iters.size() >= kCpuWindowSamples && (meas_x_iters.size() % 4 == 0)) {
                SlidingWindowRegressionStats win =
                        compute_sliding_window_regression(meas_x_iters, sweep->meas_y_ms,
                                                          kCpuWindowSamples);
                if (win.ols.valid && win.ols.r_squared >= 0.995 && win.ols.rme95_pct <= 1.5 &&
                    win.ols.mean_error_pct <= 2.0 && win.plateau_drift_pct <= 2.5) {
                    ALOGI("SwappyTest: CPU calibration converged early at %zu chunks (win R2=%.5f, "
                          "RME=%.2f%%, MeanErr=%.2f%%, drift=%.2f%%)",
                          meas_x_iters.size(), win.ols.r_squared, win.ols.rme95_pct,
                          win.ols.mean_error_pct, win.plateau_drift_pct);
                    break;
                }
            }
        }

        if ((get_time_monotonic_ns() - sweep_start_ns) >= sweep_budget_ns) {
            ALOGI("SwappyTest: CPU calibration reached 5.0s budget (%zu accepted chunks)",
                  meas_x_iters.size());
            break;
        }
    }
}

// Fits the sliding window into res: the rate, the primary core and the median
// frequency. False with res->failure set when the fit fails.
static bool fit_cpu_sweep(const CpuSweep& sweep, int ref_max_khz, CpuCalibrationResult* res,
                          LinearRegressionStats* stats_out) {
    SlidingWindowRegressionStats win =
            compute_sliding_window_regression(sweep.meas_x_iters, sweep.meas_y_ms,
                                              kCpuWindowSamples);
    const LinearRegressionStats& stats = win.ols;
    if (!stats.valid || stats.slope <= 1e-12) {
        res->failure = "cpu_fit_failed";
        return false;
    }
    *stats_out = stats;

    res->r_squared = stats.r_squared;
    res->rme95_pct = stats.rme95_pct;
    res->mean_error_pct = stats.mean_error_pct;
    res->plateau_drift_pct = win.plateau_drift_pct;
    // Slope = ms per iteration -> Rate = 1 / slope (iterations per ms)
    res->iters_per_ms = static_cast<uint64_t>(std::lround(1.0 / stats.slope));

    // Determine primary core and median frequency over the sliding window
    int best_c = -1, max_c_count = 0;
    for (auto const& [core, count] : sweep.core_counts) {
        if (count > max_c_count) {
            max_c_count = count;
            best_c = core;
        }
    }
    res->core = best_c;

    const std::vector<int>& accepted_khz = sweep.accepted_khz;
    if (!accepted_khz.empty()) {
        const size_t khz_start = (accepted_khz.size() > kCpuWindowSamples)
                ? (accepted_khz.size() - kCpuWindowSamples)
                : 0;
        std::vector<int> win_khz(accepted_khz.begin() + khz_start, accepted_khz.end());
        std::sort(win_khz.begin(), win_khz.end());
        res->freq_mhz = win_khz[win_khz.size() / 2] / 1000;
        if (ref_max_khz > 0 && win_khz.back() >= static_cast<int>(ref_max_khz * 0.95)) {
            res->reached_max_freq = true;
        }
    }
    return true;
}

// Populate per-level statistics and compute spread strictly over the sliding window (last 4 sweeps)
static void summarize_cpu_levels(const CpuSweep& sweep, const LinearRegressionStats& stats,
                                 CpuCalibrationResult* res) {
    res->levels.clear();
    std::vector<double> level_rates;
    for (int i = 0; i < 4; ++i) {
        const auto& rec = sweep.levels[i];
        if (rec.meas_ms.empty()) continue;
        const size_t total_samples = rec.meas_ms.size();
        const size_t win_count = std::min(total_samples, kCpuPerLevelWindow);
        const size_t start_k = total_samples - win_count;

        CalibrationLevelStat lvl{};
        lvl.target_ms = kCpuLoadLevelsMs[i];
        lvl.samples = static_cast<int>(win_count);
        lvl.iters_N = rec.iters.back();

        double sum_meas = 0.0;
        for (size_t k = start_k; k < total_samples; ++k) sum_meas += rec.meas_ms[k];
        lvl.mean_meas_ms = sum_meas / win_count;

        double sum_sq_diff = 0.0;
        for (size_t k = start_k; k < total_samples; ++k) {
            double diff = rec.meas_ms[k] - lvl.mean_meas_ms;
            sum_sq_diff += diff * diff;
        }
        lvl.std_ms = (win_count > 1) ? std::sqrt(sum_sq_diff / (win_count - 1)) : 0.0;

        double sum_err = 0.0, max_err = 0.0;
        for (size_t k = start_k; k < total_samples; ++k) {
            double expected_ms = stats.intercept + stats.slope * rec.iters[k];
            double err_pct = (expected_ms > 1e-12)
                    ? (std::abs(rec.meas_ms[k] - expected_ms) / expected_ms * 100.0)
                    : 0.0;
            sum_err += err_pct;
            if (err_pct > max_err) max_err = err_pct;
        }
        lvl.mean_err_pct = sum_err / win_count;
        lvl.max_err_pct = max_err;
        lvl.level_rate = (lvl.mean_meas_ms > 1e-12) ? (lvl.iters_N / lvl.mean_meas_ms) : 0.0;
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

CpuCalibrationResult run_chunked_cpu_calibration(void) {
    CpuCalibrationResult res{};
    const uint64_t start_calib_ns = get_time_monotonic_ns();
    CpuReference ref = find_cpu_reference(&res);

    ScopedCpuAffinity affinity(ref.cpus);
    res.pinned = affinity.isPinned();

    const uint64_t warmup_iters = warm_up_cpu(&ref, &res);
    if (!s_calibration_running) {
        res.failure = "aborted";
        return res;
    }

    CpuSweep sweep;
    sweep.iters_1ms = warmup_iters / 2ULL;
    if (sweep.iters_1ms < 10000ULL) sweep.iters_1ms = 100000ULL;
    sweep.seen_at_max = res.reached_max_freq;
    sweep_cpu_levels(&ref, &sweep, &res);

    res.duration_ms = (get_time_monotonic_ns() - start_calib_ns) / 1e6;

    if (sweep.meas_x_iters.size() < 8) {
        res.failure = "insufficient_chunks";
        return res;
    }

    LinearRegressionStats stats;
    if (!fit_cpu_sweep(sweep, ref.max_khz, &res, &stats)) {
        return res;
    }
    summarize_cpu_levels(sweep, stats, &res);

    ALOGI("SwappyTest: CPU calibration finished: %llu iters/ms on core %d @ %d MHz (R2=%.5f, "
          "RME=%.2f%%, MeanErr=%.2f%%, spread=%.2f%%, drift=%.2f%%, chunks=%d)",
          (unsigned long long)res.iters_per_ms, res.core, res.freq_mhz, res.r_squared,
          res.rme95_pct, res.mean_error_pct, res.spread_pct, res.plateau_drift_pct,
          res.accepted_chunks);
    return res;
}

std::string cpu_calibration_failure(const CpuCalibrationResult& cpu, bool held) {
    char reason[192];
    if (!held) return "calibration_hold_timeout";
    if (cpu.failure != nullptr || cpu.accepted_chunks < 8) {
        snprintf(reason, sizeof(reason), "cpu_calibration_unstable (accepted %d)",
                 cpu.accepted_chunks);
        return reason;
    }
    if (cpu.r_squared < 0.990 || cpu.rme95_pct > 2.0) {
        snprintf(reason, sizeof(reason), "cpu_calibration_unstable (R2=%.4f, RME=%.1f%%)",
                 cpu.r_squared, cpu.rme95_pct);
        return reason;
    }
    const int expected_max_mhz =
            (cpu.ref_max_mhz > 0) ? cpu.ref_max_mhz : s_device_global_max_freq_mhz.load();
    if (!cpu.reached_max_freq && expected_max_mhz > 0) {
        snprintf(reason, sizeof(reason),
                 "calibration never reached max frequency (peaked at %d of %d MHz)", cpu.freq_mhz,
                 expected_max_mhz);
        return reason;
    }
    if (cpu.iters_per_ms == 0) return "cpu_calibration_incomplete";
    return "";
}

} // namespace dvfs
