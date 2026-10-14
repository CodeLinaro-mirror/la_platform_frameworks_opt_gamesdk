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

#include "DvfsBenchmark.h"

#define LOG_TAG "SwappyTest"

#include <android/log.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#include "Calibration.h"
#include "Circle.h"
#include "CpuGpuFreq.h"
#include "DvfsBenchmarkInternal.h"
#include "DvfsTrace.h"
#include "Log.h"
#include "Scenario.h"
#include "Trace.h"
#include "swappy/swappy_common.h"

using dvfs::acquire_render_hold;
using dvfs::CALIB_HOLD_NONE;
using dvfs::calibration_bit;
using dvfs::calibration_kind_name;
using dvfs::calibration_report_filename;
using dvfs::clamped_pct;
using dvfs::cpu_calibration_failure;
using dvfs::cpu_cluster_count;
using dvfs::cpu_cluster_of;
using dvfs::CpuCalibrationCache;
using dvfs::CpuCalibrationResult;
using dvfs::execute_cpu_workload;
using dvfs::find_device_global_max_freq_khz;
using dvfs::find_scenario;
using dvfs::format_calibration_set;
using dvfs::get_thread_cpu_time_ns;
using dvfs::get_time_monotonic_ns;
using dvfs::gpu_calibration_failure;
using dvfs::GpuCalibrationCache;
using dvfs::GpuCalibrationResult;
using dvfs::kExternalFilesDir;
using dvfs::khz_to_mhz;
using dvfs::kMaxCpus;
using dvfs::kMaxScenarioSteps;
using dvfs::kScenarioCustom;
using dvfs::load_cpu_calibration_cache;
using dvfs::load_gpu_calibration_cache;
using dvfs::output_path;
using dvfs::path_in;
using dvfs::read_cpu_freq_khz;
using dvfs::read_cpuinfo_max_freq_khz;
using dvfs::read_gpu_freq_khz;
using dvfs::read_gpu_max_freq_khz;
using dvfs::release_render_hold;
using dvfs::request_gpu_calibration;
using dvfs::run_chunked_cpu_calibration;
using dvfs::s_calibration_hold;
using dvfs::s_calibration_running;
using dvfs::s_cooldown_duration_ms;
using dvfs::s_cpu_calibration_result;
using dvfs::s_device_global_max_freq_mhz;
using dvfs::s_gpu_calibration_result;
using dvfs::s_headroom_end_pct;
using dvfs::s_headroom_start_pct;
using dvfs::s_render_surface_height;
using dvfs::s_render_surface_width;
using dvfs::s_run_tainted;
using dvfs::s_test_mutex;
using dvfs::save_cpu_calibration_cache;
using dvfs::save_gpu_calibration_cache;
using dvfs::ScenarioDef;
using dvfs::ScenarioStep;
using dvfs::sleep_ms;
using dvfs::worst_cpu_headroom_pct;
using dvfs::write_calibration_report;

std::atomic<int32_t> dvfs::s_render_surface_width{0};
std::atomic<int32_t> dvfs::s_render_surface_height{0};

void set_render_surface_size(int32_t width, int32_t height) {
    s_render_surface_width.store(width, std::memory_order_relaxed);
    s_render_surface_height.store(height, std::memory_order_relaxed);
}

static constexpr int kFpsHistorySize = 30;
static constexpr uint32_t kRngSeed = 0xCAFEBABE;

// The run and scenario step state. The plain fields are guarded by
// s_test_mutex. The atomics are shared between the UI, render and
// calibration threads so get_ui_snapshot() reads them lock-free.
struct RunState {
    // Guarded by s_test_mutex: the selected scenario and its configuration,
    // the scenario table, the RNG behind the step jitter, and the frame-time
    // window.
    char selected_scenario_name[32] = "S1";
    char selected_scenario_desc[128] = "S1: Deterministic 39.0s CPU Ramp-Down (27, 18, 14->4 ms)";
    char output_directory[512] = "/sdcard/Android/data/com.samples.swappytest/files";
    char custom_steps_spec[1024] = "";
    int run_iteration = 1;
    ScenarioStep scenario_steps[kMaxScenarioSteps];
    uint32_t rng_state = kRngSeed;
    uint64_t frame_times_ns[kFpsHistorySize] = {0};
    int frame_time_idx = 0;
    int frame_time_count = 0;
    uint64_t last_frame_stamp_ns = 0;

    // Written once per run by mark_run_tainted(), before the release store of
    // s_run_tainted publishes it to the lock-free readers.
    char taint_reason[192] = {0};

    // The catalog entry of selected_scenario_name, or null while it is not a
    // catalog id. Catalog entries are immutable, so get_test_scenario() reads
    // its id lock-free.
    std::atomic<const ScenarioDef*> selected_scenario{nullptr};
    std::atomic<TestPhase> test_phase{TEST_PHASE_READY};
    std::atomic<bool> steps_active{false};
    // The calibrations the next run runs and stores instead of running the
    // selected scenario; none for a scenario run.
    std::atomic<CalibrationSet> calibration_run{0};
    // The calibrations the selected scenario needs on the active render API,
    // and those of them a failed scenario run found missing or invalid.
    std::atomic<CalibrationSet> required_calibrations{0};
    std::atomic<CalibrationSet> missing_calibrations{0};
    // Set with the taint by mark_run_failed(): the run produced no result.
    std::atomic<bool> run_failed{false};
    std::atomic<bool> user_30fps_limit{false};
    std::atomic<bool> active_scenario_explicit_30fps{false};
    std::atomic<bool> active_scenario_square_wave{false};
    std::atomic<int> scenario_step_count{0};
    std::atomic<int> current_step_index{0};
    std::atomic<uint64_t> current_step_start_ns{0};
    std::atomic<float> current_step_duration_sec{1.0f};
    std::atomic<float> target_workload_ms{0.0f};
    std::atomic<float> target_cpu_workload_ms{0.0f};
    std::atomic<float> target_gpu_workload_ms{0.0f};
    std::atomic<uint64_t> calibrated_iters_per_ms{0};
    std::atomic<double> calibrated_gpu_iters_per_ms{0.0};
    std::atomic<double> calibrated_gpu_fixed_ms{0.0};
    // Numbers the run's frames in the SwappyTest::Frame, CpuWork and
    // GpuSubmit slices and the run record's frame telemetry: one per frame
    // interval shorter than kMaxRunFrameIntervalNs while scenario steps run.
    std::atomic<uint32_t> run_frame_count{0};
};
static RunState s_run;

// What the render and calibration threads measured, read lock-free by
// get_ui_snapshot().
struct UiTelemetry {
    std::atomic<int> current_cpu_core{-1};
    std::atomic<int> current_cpu_cluster{-1};
    std::atomic<int> current_cpu_freq_mhz{-1};
    std::atomic<int> current_cpu_max_freq_mhz{-1};
    std::atomic<float> cpu_freq_ratio_pct{100.0f};
    std::atomic<float> current_cpu_freq_pct{0.0f};
    std::atomic<float> current_gpu_freq_pct{-1.0f};
    std::atomic<float> last_frame_expected_ms{0.0f};
    std::atomic<float> last_frame_real_cpu_ms{0.0f};
    std::atomic<float> actual_workload_ms{0.0f};
    std::atomic<float> actual_gpu_workload_ms{0.0f};
    std::atomic<float> expected_fps{60.0f};
    std::atomic<float> measured_real_fps{60.0f};
    std::atomic<float> cooldown_progress_sec{0.0f};
    std::atomic<int> cooldown_headroom_pct{-1};
    std::atomic<int> calib_core{-1};
    std::atomic<int> calib_freq_mhz{-1};
    std::atomic<bool> calib_at_max_freq{true};
    std::atomic<int32_t> current_gpu_workload{0};
    // Swappy postWait GPU timing sample: -1 while the previous frame is still
    // on the GPU, 0 before the first report (none come while Swappy is
    // disabled). Used for runtime per-frame telemetry only; calibration uses
    // fence-only offscreen passes.
    std::atomic<int64_t> last_swappy_gpu_time_ns{0};
    // The CalibrationStatus of each CalibrationKind's cache.
    std::atomic<int> calibration_status[CALIBRATION_KIND_COUNT] = {};
};
static UiTelemetry s_ui;

static std::atomic<int> s_active_render_api{SWAPPY_RENDER_API_GLES};
std::atomic<bool> dvfs::s_calibration_running{false};

void set_active_render_api(SwappyRenderApi api) {
    const int prev = s_active_render_api.exchange(static_cast<int>(api), std::memory_order_relaxed);
    if (prev != static_cast<int>(api) && !s_calibration_running.load(std::memory_order_relaxed)) {
        s_run.test_phase.store(TEST_PHASE_READY, std::memory_order_release);
    }
}

SwappyRenderApi get_active_render_api(void) {
    return static_cast<SwappyRenderApi>(s_active_render_api.load(std::memory_order_relaxed));
}

const char* get_active_render_api_name(void) {
    return (get_active_render_api() == SWAPPY_RENDER_API_VULKAN) ? "vulkan" : "gles";
}

// The GPU calibration kind of the active render API.
static CalibrationKind active_gpu_calibration_kind(void) {
    return (get_active_render_api() == SWAPPY_RENDER_API_VULKAN) ? CALIBRATION_VULKAN
                                                                 : CALIBRATION_GLES;
}

// The longest frame interval RunState::run_frame_count counts.
static constexpr uint64_t kMaxRunFrameIntervalNs = 1000000000ULL;

void set_run_iteration(int iteration) {
    std::lock_guard<std::mutex> lock(s_test_mutex);
    s_run.run_iteration = (iteration > 0) ? iteration : 1;
}

// Only the trace macros use it, and they compile out with tracing disabled.
[[maybe_unused]] static const char* get_phase_name_for_step(int step_idx, int total_steps) {
    if (step_idx <= 0) return "STEADY_PRE";
    if (step_idx == 1 || (step_idx == 2 && total_steps > 3)) return "SPIKE";
    return "RECOVERY";
}

// Global test states. s_test_mutex guards the plain fields of s_run; the
// atomics here, in s_run and in s_ui are read lock-free by get_ui_snapshot().
std::mutex dvfs::s_test_mutex;

std::atomic<int> dvfs::s_device_global_max_freq_mhz{-1};
std::atomic<int> dvfs::s_headroom_start_pct{-1};
std::atomic<int> dvfs::s_headroom_end_pct{-1};

// The calibration thread start_benchmark_run() spawns and
// stop_benchmark_run() joins.
struct CalibrationThread {
    pthread_t thread;
    bool joinable = false;
};
static CalibrationThread s_calibration_thread;

// The external big-core boost helper for H10. It burns CPU while until_ns
// lies in the future.
struct BoostThread {
    pthread_t thread;
    std::atomic<bool> started{false};
    std::atomic<bool> running{false};
    std::atomic<uint64_t> until_ns{0};
};
static BoostThread s_boost;
// The boost thread burns kBoostChunkIterations per loop while a pulse is
// active and otherwise polls every kBoostIdlePollMs.
static constexpr uint64_t kBoostChunkIterations = 200000ULL;
static constexpr long kBoostIdlePollMs = 20;
static constexpr uint64_t kBoostPulseNs = 500000000ULL;

void set_user_30fps_limit(bool enabled) {
    s_run.user_30fps_limit = enabled;
}

static void* external_boost_thread_func(void* arg) {
    (void)arg;
    while (s_boost.running) {
        uint64_t now_ns = get_time_monotonic_ns();
        uint64_t until_ns = s_boost.until_ns;
        if (until_ns > now_ns) {
            execute_cpu_workload(kBoostChunkIterations);
        } else {
            sleep_ms(kBoostIdlePollMs);
        }
    }
    return NULL;
}

static void ensure_boost_thread_started(void) {
    if (!s_boost.started) {
        s_boost.running = true;
        s_boost.started =
                (pthread_create(&s_boost.thread, NULL, external_boost_thread_func, NULL) == 0);
    }
}

void record_swappy_post_wait_sample(int64_t cpu_time_ns, int64_t gpu_time_ns) {
    (void)cpu_time_ns;
    s_ui.last_swappy_gpu_time_ns = gpu_time_ns;
}

// The display refresh period Swappy measured, or one 60 Hz period until
// Swappy has been initialised with a display.
static constexpr uint64_t kDefaultRefreshPeriodNs = 16666666ULL;
static uint64_t get_display_refresh_period_ns(void) {
    const uint64_t refresh_period_ns = swappy_backend_get_refresh_period_ns();
    return (refresh_period_ns > 0) ? refresh_period_ns : kDefaultRefreshPeriodNs;
}

int32_t get_current_gpu_workload(void) {
    return s_ui.current_gpu_workload;
}

// Run-level validity
std::atomic<bool> dvfs::s_run_tainted{false};

static constexpr int kThermalHeadroomOkPct = 99;
static constexpr uint64_t kCooldownTimeoutNs = 60000000000ULL;
static constexpr long kCooldownPollMs = 250;

// Lock-free so it can be called with s_test_mutex held. Its callers never
// overlap: a calibration run taints only on the calibration thread, a
// scenario run only under s_test_mutex. The release store publishes
// s_run.taint_reason to the lock-free UI readers.
static void mark_run_tainted(const char* reason) {
    if (s_run_tainted) return;
    snprintf(s_run.taint_reason, sizeof(s_run.taint_reason), "%s", reason);
    s_run_tainted.store(true, std::memory_order_release);
    SWAPPY_TEST_TRACE_INSTANT("SwappyTest::Taint|%s", reason);
    ALOGI("SwappyTest: run TAINTED: %s", reason);
}

// Taints the run with reason and marks it failed: it produced no result.
static void mark_run_failed(const char* reason) {
    s_run.run_failed.store(true, std::memory_order_relaxed);
    mark_run_tainted(reason);
}

// Scenario Catalog Definitions (T7)
static std::atomic<bool> s_active_scenario_uses_gpu{false};

static uint32_t next_rng_u32(void) {
    uint32_t x = s_run.rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_run.rng_state = x;
    return x;
}

bool is_scenario_explicit_30fps(void) {
    return s_run.active_scenario_explicit_30fps;
}

float get_last_swappy_gpu_ms(void) {
    int64_t gpu_ns = s_ui.last_swappy_gpu_time_ns;
    return (gpu_ns > 0) ? ((float)gpu_ns / 1000000.0f) : 0.0f;
}

void set_output_directory(const char* dir_path) {
    if (dir_path && dir_path[0] != '\0') {
        std::lock_guard<std::mutex> lock(s_test_mutex);
        snprintf(s_run.output_directory, sizeof(s_run.output_directory), "%s", dir_path);
        ALOGI("SwappyTest: JSON output directory set to %s", s_run.output_directory);
    }
}

void set_calibration_run(CalibrationSet kinds) {
    s_run.calibration_run.store(kinds & dvfs::kAllCalibrations, std::memory_order_relaxed);
}

void set_custom_steps(const char* steps_spec) {
    std::lock_guard<std::mutex> lock(s_test_mutex);
    if (!steps_spec) {
        s_run.custom_steps_spec[0] = '\0';
    } else {
        snprintf(s_run.custom_steps_spec, sizeof(s_run.custom_steps_spec), "%s", steps_spec);
        ALOGI("SwappyTest: Custom steps spec set to '%s'", s_run.custom_steps_spec);
    }
}

// Stores the selected scenario name and publishes its catalog entry, if any,
// to get_test_scenario(). Called under s_test_mutex.
static void set_selected_scenario_name_locked(const char* name) {
    snprintf(s_run.selected_scenario_name, sizeof(s_run.selected_scenario_name), "%s", name);
    s_run.selected_scenario.store(find_scenario(s_run.selected_scenario_name,
                                                /*ignore_case=*/false),
                                  std::memory_order_release);
}

// A catalog id is returned lock-free from its immutable catalog entry. Any
// other name, stored verbatim by set_test_scenario() until the run resolves
// it, is returned as a per-thread snapshot copied under s_test_mutex, so
// set_test_scenario() on another thread cannot rewrite the string while the
// caller reads it; valid until the calling thread's next call.
static const char* get_test_scenario(void) {
    if (const ScenarioDef* def = s_run.selected_scenario.load(std::memory_order_acquire)) {
        return def->id;
    }
    thread_local char name[sizeof(s_run.selected_scenario_name)];
    std::lock_guard<std::mutex> lock(s_test_mutex);
    snprintf(name, sizeof(name), "%s", s_run.selected_scenario_name);
    return name;
}

// Once CUSTOM is selected, its entry describes the configured steps. Called
// under s_test_mutex.
static const char* scenario_description_locked(const ScenarioDef& def) {
    if ((def.flags & kScenarioCustom) && strcasecmp(s_run.selected_scenario_name, "CUSTOM") == 0 &&
        s_run.selected_scenario_desc[0] != '\0') {
        return s_run.selected_scenario_desc;
    }
    return def.description;
}

int get_scenario_count(void) {
    return dvfs::scenario_count();
}

const char* get_scenario_id_by_index(int index) {
    if (index >= 0 && index < get_scenario_count()) {
        return dvfs::scenario_at(index).id;
    }
    return nullptr;
}

// The CUSTOM entry returns a per-thread snapshot of the configured description,
// copied under s_test_mutex; valid until the calling thread's next call. Unlike
// the catalog descriptions it is rebuilt from the custom steps spec, so it has
// no immutable copy a lock-free pointer could point at without leaking one
// string per change.
const char* get_scenario_description_by_index(int index) {
    if (index < 0 || index >= get_scenario_count()) return nullptr;
    const ScenarioDef& def = dvfs::scenario_at(index);
    if (!(def.flags & kScenarioCustom)) return def.description;
    thread_local char desc[sizeof(s_run.selected_scenario_desc)];
    std::lock_guard<std::mutex> lock(s_test_mutex);
    snprintf(desc, sizeof(desc), "%s", scenario_description_locked(def));
    return desc;
}

static const char* find_scenario_description_by_id(const char* name) {
    const ScenarioDef* def = find_scenario(name, /*ignore_case=*/true);
    return def ? scenario_description_locked(*def) : nullptr;
}

// Makes the given step the active workload target and applies any per-step Swappy overrides.
static void stage_scenario_step(const ScenarioStep* step) {
    s_run.target_cpu_workload_ms = (float)step->target_cpu_ms;
    s_run.target_gpu_workload_ms = (float)step->target_gpu_ms;
    s_run.target_workload_ms = (step->target_gpu_ms > step->target_cpu_ms)
            ? (float)step->target_gpu_ms
            : (float)step->target_cpu_ms;
    s_run.current_step_duration_sec = step->duration_sec;

    if (step->auto_pipeline >= 0) {
        const char* mode_str =
                (step->auto_pipeline == 0) ? "off" : ((step->auto_pipeline == 1) ? "on" : "auto");
        bool pipe_auto = (step->auto_pipeline == 2);
        setenv("SWAPPY_PIPELINE_MODE", mode_str, 1);
        ALOGI("SwappyTest: Step override -> pipeline=%s (setAutoPipelineMode(%s))", mode_str,
              pipe_auto ? "true" : "false");
        swappy_backend_set_auto_pipeline_mode(pipe_auto);
    }
    if (step->auto_swap_interval >= 0) {
        bool auto_si = (step->auto_swap_interval != 0);
        ALOGI("SwappyTest: Step override -> setAutoSwapInterval(%s)", auto_si ? "true" : "false");
        swappy_backend_set_auto_swap_interval(auto_si);
    }
    if (step->buffer_stuffing_fix_wait >= 0) {
        ALOGI("SwappyTest: Step override -> setBufferStuffingFixWait(%d)",
              step->buffer_stuffing_fix_wait);
        swappy_backend_set_buffer_stuffing_fix_wait(step->buffer_stuffing_fix_wait);
    }
    if (step->target_fps > 0) {
        uint64_t interval_ns =
                static_cast<uint64_t>(1000000000.0 / static_cast<double>(step->target_fps));
        ALOGI("SwappyTest: Step override -> setSwapIntervalNS(%llu) (%d FPS)",
              (unsigned long long)interval_ns, step->target_fps);
        swappy_backend_set_swap_interval_ns(interval_ns);
        s_run.active_scenario_explicit_30fps = (step->target_fps <= 30);
    }
}

// Keeps the boost thread busy for the next 500 ms: an external CPU load that
// briefly lifts the clocks the render thread runs at.
static void fire_external_boost_pulse(uint64_t now_ns) {
    ALOGI("SwappyTest: Step %d firing 500 ms external big-core boost pulse",
          s_run.current_step_index.load());
    s_boost.until_ns = now_ns + kBoostPulseNs;
}

static void populate_active_scenario_steps(void) {
    s_run.current_step_index = 0;
    s_run.rng_state = kRngSeed;

    // Only CUSTOM matches case-insensitively.
    const char* sc = s_run.selected_scenario_name;
    const ScenarioDef* def = find_scenario(sc, /*ignore_case=*/strcasecmp(sc, "CUSTOM") == 0);
    if (def == nullptr) {
        ALOGE("SwappyTest: Unrecognized scenario '%s', defaulting to S1", sc);
        def = &dvfs::scenario_at(0);
    }
    set_selected_scenario_name_locked(def->id);

    const std::vector<ScenarioStep> steps =
            dvfs::build_scenario_steps(*def, s_run.custom_steps_spec);
    std::copy(steps.begin(), steps.end(), s_run.scenario_steps);
    s_run.scenario_step_count = static_cast<int>(steps.size());

    if (def->flags & kScenarioCustom) {
        float total_sec = 0.0f;
        for (const ScenarioStep& step : steps) {
            total_sec += step.duration_sec;
        }
        snprintf(s_run.selected_scenario_desc, sizeof(s_run.selected_scenario_desc),
                 "CUSTOM (%d steps, %.1fs): %s", s_run.scenario_step_count.load(), total_sec,
                 s_run.custom_steps_spec[0] != '\0' ? s_run.custom_steps_spec : "5s:cpu=0,gpu=0");
    } else {
        snprintf(s_run.selected_scenario_desc, sizeof(s_run.selected_scenario_desc), "%s",
                 def->description);
    }

    const dvfs::ScenarioTraits traits = dvfs::scenario_traits(*def, steps);
    s_active_scenario_uses_gpu = traits.uses_gpu;
    s_run.required_calibrations =
            dvfs::required_calibrations(traits, active_gpu_calibration_kind());
    s_run.active_scenario_explicit_30fps = traits.explicit_30fps;
    s_run.active_scenario_square_wave = traits.square_wave;
    if (traits.needs_boost) {
        ensure_boost_thread_started();
    }
}

void set_test_scenario(const char* scenario_name) {
    std::lock_guard<std::mutex> lock(s_test_mutex);
    if (!scenario_name || scenario_name[0] == '\0') {
        set_selected_scenario_name_locked("S1");
    } else {
        set_selected_scenario_name_locked(scenario_name);
    }
    const char* desc = find_scenario_description_by_id(s_run.selected_scenario_name);
    // For CUSTOM the lookup can return s_run.selected_scenario_desc itself.
    if (desc && desc != s_run.selected_scenario_desc) {
        snprintf(s_run.selected_scenario_desc, sizeof(s_run.selected_scenario_desc), "%s", desc);
    }
}

// A file in s_run.output_directory. Called under s_test_mutex.
std::string dvfs::output_path(const char* filename) {
    return path_in(s_run.output_directory, filename);
}

// Ends a scenario run: samples the final CPU headroom, emits the
// SwappyTest::RunEnd trace marker before closing SwappyTest::Run, and logs a
// single SCENARIO_RESULT line on the SwappyTest tag.
static void complete_scenario_run(void) {
    s_headroom_end_pct = worst_cpu_headroom_pct();
    dvfs::trace_cpu_headroom_pct(s_headroom_end_pct, /*force=*/true);

    char reason[sizeof(s_run.taint_reason)];
    snprintf(reason, sizeof(reason), "%s",
             s_run.taint_reason[0] != '\0' ? s_run.taint_reason : "none");
    for (char* c = reason; *c != '\0'; ++c) {
        if (*c == '|') *c = '/';
    }

    if (SWAPPY_TEST_TRACE_ENABLED()) {
        SWAPPY_TEST_TRACE_INSTANT("SwappyTest::RunEnd|tainted=%d|run_failed=%d|headroom_end=%d|"
                                  "reason=%s",
                                  s_run_tainted.load() ? 1 : 0, s_run.run_failed.load() ? 1 : 0,
                                  s_headroom_end_pct.load(), reason);
    }

    SWAPPY_TEST_END_RUN();

    const char* status =
            s_run.run_failed.load() ? "failed" : (s_run_tainted.load() ? "tainted" : "ok");
    ALOGI("SCENARIO_RESULT status=%s scenario=%s api=%s pacing=%s headroom_start=%d "
          "headroom_end=%d tainted=%d run_failed=%d reason=%s",
          status, s_run.selected_scenario_name, get_active_render_api_name(),
          swappy_backend_get_pacing_strategy_name(), s_headroom_start_pct.load(),
          s_headroom_end_pct.load(), s_run_tainted.load() ? 1 : 0, s_run.run_failed.load() ? 1 : 0,
          reason);
}

double dvfs::s_cooldown_duration_ms = 0.0;

static void run_cooldown_phase(void) {
    s_run.test_phase = TEST_PHASE_COOLDOWN;
    s_ui.cooldown_progress_sec = 0.0f;
    s_cooldown_duration_ms = 0.0;

    const int initial = worst_cpu_headroom_pct();
    s_ui.cooldown_headroom_pct = initial;

    if (initial < 0) {
        ALOGI("SwappyTest: cooldown skipped, cpufreq scaling_max_freq sysfs unreadable (ADPF "
              "headroom unsupported, not tainted)");
        return;
    }

    if (initial >= kThermalHeadroomOkPct) {
        ALOGI("SwappyTest: cooldown not needed, CPU headroom already %d%%", initial);
        return;
    }

    ALOGI("SwappyTest: cooling down, CPU headroom %d%% (waiting for >= %d%%)", initial,
          kThermalHeadroomOkPct);

    const uint64_t start_ns = get_time_monotonic_ns();
    while (s_calibration_running) {
        const uint64_t elapsed_ns = get_time_monotonic_ns() - start_ns;
        s_ui.cooldown_progress_sec = (float)elapsed_ns / 1e9f;
        s_cooldown_duration_ms = static_cast<double>(elapsed_ns) / 1e6;

        const int pct = worst_cpu_headroom_pct();
        s_ui.cooldown_headroom_pct = pct;
        if (pct < 0 || pct >= kThermalHeadroomOkPct) {
            ALOGI("SwappyTest: cooled down after %.1f s, CPU headroom %d%%",
                  (float)elapsed_ns / 1e9f, pct);
            return;
        }

        if (elapsed_ns >= kCooldownTimeoutNs) {
            if (pct > 0 && pct < kThermalHeadroomOkPct) {
                char reason[192];
                snprintf(reason, sizeof(reason),
                         "device still throttled after %.0f s of cooldown (CPU headroom %d%%)",
                         (float)elapsed_ns / 1e9f, pct);
                mark_run_tainted(reason);
            }
            return;
        }

        sleep_ms(kCooldownPollMs);
    }
}

// The calibrated workload rates and where they came from.
struct CalibrationResult {
    uint64_t iters_per_ms = 0;
    double gpu_iters_per_ms = 0.0;
    double gpu_fixed_ms = 0.0;
    int best_core = -1;
    int best_freq_mhz = -1;
    bool reached_max_freq = false;
    CpuCalibrationResult cpu{};
    GpuCalibrationResult gpu{};
};

static int64_t render_surface_pixels(void) {
    return static_cast<int64_t>(s_render_surface_width.load(std::memory_order_relaxed)) *
            s_render_surface_height.load(std::memory_order_relaxed);
}

// Loads every calibration cache, publishes their states to get_ui_snapshot()
// and fills calib from the CPU cache and the active render API's GPU cache.
// Returns the kinds whose cache is valid.
static CalibrationSet load_calibration_caches(CalibrationResult* calib) {
    CalibrationSet valid = 0;
    CpuCalibrationCache cpu;
    const CalibrationStatus cpu_status = load_cpu_calibration_cache(&cpu);
    s_ui.calibration_status[CALIBRATION_CPU] = cpu_status;
    if (cpu_status == CALIBRATION_STATUS_VALID) {
        valid |= calibration_bit(CALIBRATION_CPU);
        calib->iters_per_ms = cpu.iters_per_ms;
        calib->best_core = cpu.core;
        calib->best_freq_mhz = cpu.freq_mhz;
        calib->reached_max_freq = true;
        calib->cpu.iters_per_ms = cpu.iters_per_ms;
        calib->cpu.mode = cpu.mode;
        calib->cpu.core = cpu.core;
        calib->cpu.freq_mhz = cpu.freq_mhz;
        calib->cpu.r_squared = cpu.r_squared;
        calib->cpu.rme95_pct = cpu.rme95_pct;
        calib->cpu.mean_error_pct = cpu.mean_error_pct;
        calib->cpu.accepted_chunks = cpu.accepted_chunks;
        calib->cpu.reached_max_freq = true;
    }
    // Both render APIs draw to the same surface.
    const int64_t pixels = render_surface_pixels();
    for (CalibrationKind kind : {CALIBRATION_GLES, CALIBRATION_VULKAN}) {
        GpuCalibrationCache gpu;
        const CalibrationStatus status = load_gpu_calibration_cache(kind, pixels, &gpu);
        s_ui.calibration_status[kind] = status;
        if (status != CALIBRATION_STATUS_VALID) continue;
        valid |= calibration_bit(kind);
        if (kind == active_gpu_calibration_kind()) {
            calib->gpu_iters_per_ms = gpu.iters_per_ms;
            calib->gpu_fixed_ms = gpu.fixed_ms;
            calib->gpu.iters_per_ms = gpu.iters_per_ms;
            calib->gpu.fixed_ms = gpu.fixed_ms;
            calib->gpu.pixels = gpu.pixels;
            calib->gpu.timer = gpu.timer;
            calib->gpu.r_squared = gpu.r_squared;
            calib->gpu.rme95_pct = gpu.rme95_pct;
            calib->gpu.mean_error_pct = gpu.mean_error_pct;
            calib->gpu.accepted_samples = gpu.accepted_samples;
        }
    }
    return valid;
}

void refresh_calibration_status(void) {
    CalibrationResult unused;
    load_calibration_caches(&unused);
}

// Cools down, then runs the calibrations in kinds while the render thread
// holds: the CPU one on this thread, the active render API's GPU one on the
// render thread. *held reports whether the render thread held. False when
// the run stopped during the cooldown.
static bool run_fresh_calibration(CalibrationSet kinds, CalibrationResult* calib, bool* held) {
    run_cooldown_phase();
    if (!s_calibration_running) return false;
    s_headroom_start_pct = worst_cpu_headroom_pct();
    dvfs::trace_cpu_headroom_pct(s_headroom_start_pct, /*force=*/true);

    ALOGI("Calibration thread started (%s). Requesting render thread hold...",
          format_calibration_set(kinds).c_str());

    *held = acquire_render_hold();

    if (kinds & calibration_bit(CALIBRATION_CPU)) {
        ALOGI("SwappyTest: Running linearity-verified CPU calibration...");
        calib->cpu = run_chunked_cpu_calibration();
        calib->iters_per_ms = calib->cpu.iters_per_ms;
        calib->best_core = calib->cpu.core;
        calib->best_freq_mhz = calib->cpu.freq_mhz;
        calib->reached_max_freq = calib->cpu.reached_max_freq;
        std::lock_guard<std::mutex> lock(s_test_mutex);
        s_cpu_calibration_result = calib->cpu;
    }

    if (kinds & calibration_bit(active_gpu_calibration_kind())) {
        if (*held) {
            ALOGI("SwappyTest: Requesting fence-only offscreen GPU calibration on render "
                  "thread...");
            calib->gpu = request_gpu_calibration();
            calib->gpu_iters_per_ms = calib->gpu.iters_per_ms;
            calib->gpu_fixed_ms = calib->gpu.fixed_ms;
        } else {
            calib->gpu.failure = "hold_timeout";
        }
        std::lock_guard<std::mutex> lock(s_test_mutex);
        s_gpu_calibration_result = calib->gpu;
    }

    release_render_hold();
    swappy_backend_reset_frame_pacing();
    return true;
}

// The taint reason prefix of a scenario run started without the calibrations
// it needs; the missing kinds follow, as in "no_calibration:cpu,vulkan".
static constexpr char kNoCalibrationReason[] = "no_calibration";

// Fails a scenario run started without valid caches of the calibrations in
// missing: the workload never runs and SCENARIO_RESULT reports the failure.
static void fail_run_without_calibration(CalibrationSet missing) {
    std::lock_guard<std::mutex> lock(s_test_mutex);
    const std::string kinds = format_calibration_set(missing);
    ALOGE("SwappyTest: scenario %s not run on %s: missing or invalid calibrations: %s. Run them "
          "first (Calibrate CPU / Calibrate GPU buttons, --es calibrate %s, or "
          "run_swappy_matrix.py --calibrate-only %s).",
          s_run.selected_scenario_name, get_active_render_api_name(), kinds.c_str(), kinds.c_str(),
          kinds.c_str());
    s_run.missing_calibrations = missing;
    char reason[64];
    snprintf(reason, sizeof(reason), "%s:%s", kNoCalibrationReason, kinds.c_str());
    mark_run_failed(reason);
    complete_scenario_run();
    // Release-publishes the run state, as advance_scenario_step() does.
    s_run.test_phase.store(TEST_PHASE_COMPLETE, std::memory_order_release);
}

// Loads the stored calibrations, fails the run if any calibration the
// scenario needs is missing or invalid, and otherwise stages step 0.
static void start_scenario_run(void) {
    CalibrationResult calib;
    const CalibrationSet valid = load_calibration_caches(&calib);
    const CalibrationSet missing = s_run.required_calibrations.load() & ~valid;
    if (missing != 0) {
        fail_run_without_calibration(missing);
        return;
    }

    s_headroom_start_pct = worst_cpu_headroom_pct();
    dvfs::trace_cpu_headroom_pct(s_headroom_start_pct, /*force=*/true);
    s_headroom_end_pct = s_headroom_start_pct.load();
    ALOGI("SwappyTest: Using cached calibrations %s (cpu=%llu, gpu=%.3f, fixed=%.3f ms, "
          "core=%d @ %d MHz)",
          format_calibration_set(valid).c_str(), (unsigned long long)calib.iters_per_ms,
          calib.gpu_iters_per_ms, calib.gpu_fixed_ms, calib.best_core, calib.best_freq_mhz);

    std::lock_guard<std::mutex> lock(s_test_mutex);
    if (s_run.scenario_step_count == 0) {
        populate_active_scenario_steps();
    }
    s_cpu_calibration_result = calib.cpu;
    s_gpu_calibration_result = calib.gpu;
    s_run.calibrated_iters_per_ms = calib.iters_per_ms;
    s_run.calibrated_gpu_iters_per_ms = calib.gpu_iters_per_ms;
    s_run.calibrated_gpu_fixed_ms = calib.gpu_fixed_ms;
    s_ui.calib_core = calib.best_core;
    s_ui.calib_freq_mhz = calib.best_freq_mhz;
    s_ui.calib_at_max_freq = true;
    s_run.current_step_index = 0;
    s_run.test_phase = TEST_PHASE_RUNNING;
    s_run.current_step_start_ns = 0;

    const bool limit_30fps = s_run.active_scenario_explicit_30fps || s_run.user_30fps_limit;
    swappy_backend_set_swap_interval_ns(limit_30fps ? SWAPPY_SWAP_30FPS : SWAPPY_SWAP_60FPS);
    swappy_backend_set_auto_swap_interval(true);
    stage_scenario_step(&s_run.scenario_steps[0]);

    SWAPPY_TEST_UPDATE_PHASE(get_phase_name_for_step(0, s_run.scenario_step_count));
    s_run.steps_active.store(true, std::memory_order_release);
}

// Why the calibration of kind must not be stored, or "" when it may.
static std::string calibration_failure(CalibrationKind kind, const CalibrationResult& calib,
                                       bool held) {
    // A cooldown that left the device throttled fails every kind.
    if (s_run_tainted) return s_run.taint_reason;
    if (kind == CALIBRATION_CPU) return cpu_calibration_failure(calib.cpu, held);
    if (kind != active_gpu_calibration_kind()) {
        return std::string("render_api_mismatch (active ") + get_active_render_api_name() + ")";
    }
    return gpu_calibration_failure(calib.gpu, held);
}

// Ends a calibration run: stores the cache of each calibrated kind that is
// valid, so one failing kind leaves the others' caches intact, and fails the
// run with the reasons of the kinds that failed. Logs one CALIBRATION_RESULT
// line per kind.
static void finish_calibration_run(CalibrationSet kinds, const CalibrationResult& calib,
                                   bool held) {
    std::string failures;
    for (int k = 0; k < CALIBRATION_KIND_COUNT; ++k) {
        const CalibrationKind kind = static_cast<CalibrationKind>(k);
        if ((kinds & calibration_bit(kind)) == 0) continue;
        const char* name = calibration_kind_name(kind);
        const std::string failure = calibration_failure(kind, calib, held);
        if (!failure.empty()) {
            if (!failures.empty()) failures += "; ";
            failures += std::string(name) + ": " + failure;
            ALOGE("SwappyTest: CALIBRATION_RESULT status=failed kind=%s api=%s reason=%s", name,
                  get_active_render_api_name(), failure.c_str());
        } else if (kind == CALIBRATION_CPU) {
            save_cpu_calibration_cache(calib.cpu, s_device_global_max_freq_mhz.load());
            ALOGI("SwappyTest: CALIBRATION_RESULT status=ok kind=%s api=%s cpu_iters_per_ms=%llu "
                  "core=%d freq_mhz=%d",
                  name, get_active_render_api_name(), (unsigned long long)calib.iters_per_ms,
                  calib.best_core, calib.best_freq_mhz);
        } else {
            save_gpu_calibration_cache(kind, calib.gpu, render_surface_pixels());
            ALOGI("SwappyTest: CALIBRATION_RESULT status=ok kind=%s api=%s gpu_iters_per_ms=%.3f "
                  "gpu_fixed_ms=%.3f",
                  name, get_active_render_api_name(), calib.gpu_iters_per_ms, calib.gpu_fixed_ms);
        }
    }
    refresh_calibration_status();

    std::lock_guard<std::mutex> lock(s_test_mutex);
    s_run.calibrated_iters_per_ms = calib.iters_per_ms;
    s_run.calibrated_gpu_iters_per_ms = calib.gpu_iters_per_ms;
    s_run.calibrated_gpu_fixed_ms = calib.gpu_fixed_ms;
    s_ui.calib_core = calib.best_core;
    s_ui.calib_freq_mhz = calib.best_freq_mhz;
    s_ui.calib_at_max_freq = calib.reached_max_freq || (s_device_global_max_freq_mhz <= 0);
    s_calibration_running = false;
    if (!failures.empty()) mark_run_failed(failures.c_str());
    SWAPPY_TEST_END_RUN();
    s_run.test_phase.store(TEST_PHASE_COMPLETE, std::memory_order_release);
}

static void* calibration_thread_func(void* arg) {
    (void)arg;

    const CalibrationSet kinds = s_run.calibration_run.load(std::memory_order_relaxed);
    CalibrationResult calib;
    bool held = false;
    if (!run_fresh_calibration(kinds, &calib, &held)) return NULL;

    if (!s_calibration_running) return NULL;

    s_headroom_end_pct = worst_cpu_headroom_pct();
    write_calibration_report(kinds, calib.cpu, calib.gpu, held);
    finish_calibration_run(kinds, calib, held);
    return NULL;
}

void start_benchmark_run(void) {
    CalibrationSet kinds = 0;
    {
        std::lock_guard<std::mutex> lock(s_test_mutex);
        if (s_calibration_running) {
            return;
        }
        // A calibration run replaces the reports of the kinds it calibrates;
        // a scenario run keeps those of the calibrations it reuses.
        kinds = s_run.calibration_run.load(std::memory_order_relaxed);
        if (kinds != 0) {
            std::vector<std::string> reports = {"calibration_report.txt"};
            for (int k = 0; k < CALIBRATION_KIND_COUNT; ++k) {
                const CalibrationKind kind = static_cast<CalibrationKind>(k);
                if (kinds & calibration_bit(kind)) {
                    reports.push_back(calibration_report_filename(kind));
                }
            }
            for (const std::string& report : reports) {
                remove(output_path(report.c_str()).c_str());
                remove(path_in(kExternalFilesDir, report.c_str()).c_str());
            }
        }

        SWAPPY_TEST_START_RUN(s_run.selected_scenario_name, s_run.run_iteration);

        populate_active_scenario_steps();
        s_run.steps_active = false;
        s_calibration_hold.store(CALIB_HOLD_NONE, std::memory_order_release);
        if (kinds & calibration_bit(CALIBRATION_CPU)) {
            s_cpu_calibration_result = CpuCalibrationResult{};
        }
        if (kinds & (calibration_bit(CALIBRATION_GLES) | calibration_bit(CALIBRATION_VULKAN))) {
            s_gpu_calibration_result = GpuCalibrationResult{};
        }
        s_ui.current_gpu_workload = 0;
        s_run_tainted = false;
        s_run.run_failed = false;
        s_run.missing_calibrations = 0;
        s_run.taint_reason[0] = '\0';
        s_headroom_start_pct = -1;
        s_headroom_end_pct = -1;
        s_ui.cooldown_progress_sec = 0.0f;
        s_ui.cooldown_headroom_pct = -1;
        s_run.target_workload_ms = 0.0f;
        s_run.target_cpu_workload_ms = 0.0f;
        s_run.target_gpu_workload_ms = 0.0f;
        s_ui.actual_workload_ms = 0.0f;
        s_ui.actual_gpu_workload_ms = 0.0f;
        s_run.current_step_duration_sec = 3.0f;
        s_run.frame_time_idx = 0;
        s_run.frame_time_count = 0;
        s_run.last_frame_stamp_ns = 0;
        s_ui.measured_real_fps = 60.0f;
        s_run.run_frame_count = 0;
        if (kinds != 0) {
            s_calibration_running = true;
            s_run.test_phase = TEST_PHASE_COOLDOWN;
        }
    }

    int global_max_khz = find_device_global_max_freq_khz();
    s_device_global_max_freq_mhz = khz_to_mhz(global_max_khz);
    ALOGI("SwappyTest: Device global max CPU frequency = %d MHz",
          s_device_global_max_freq_mhz.load());

    if (kinds == 0) {
        start_scenario_run();
        return;
    }

    s_calibration_thread.joinable = (pthread_create(&s_calibration_thread.thread, NULL,
                                                    calibration_thread_func, NULL) == 0);
}

void stop_benchmark_run(void) {
    s_calibration_running = false;
    s_run.steps_active.store(false, std::memory_order_relaxed);
    if (s_calibration_thread.joinable) {
        pthread_join(s_calibration_thread.thread, NULL);
        s_calibration_thread.joinable = false;
    }
    if (s_boost.started) {
        s_boost.until_ns = 0;
        s_boost.running = false;
        pthread_join(s_boost.thread, NULL);
        s_boost.started = false;
    }
}

static float step_time_remaining_sec(void) {
    uint64_t now_ns = get_time_monotonic_ns();
    if (s_run.test_phase == TEST_PHASE_RUNNING) {
        if (s_run.current_step_start_ns == 0) {
            return s_run.current_step_duration_sec;
        }
        uint64_t elapsed = now_ns - s_run.current_step_start_ns;
        float rem = s_run.current_step_duration_sec - (float)elapsed / 1e9f;
        return rem > 0.0f ? rem : 0.0f;
    }
    return 0.0f;
}

static float step_duration_sec(void) {
    if (s_run.test_phase == TEST_PHASE_RUNNING) {
        return s_run.current_step_duration_sec;
    }
    return 0.0f;
}

// The run diverged when a 60 FPS target measures below 45 FPS.
static constexpr float kDivergenceMinExpectedFps = 55.0f;
static constexpr float kDivergenceMaxRealFps = 45.0f;

bool get_is_fps_diverged(void) {
    return (s_ui.expected_fps >= kDivergenceMinExpectedFps &&
            s_ui.measured_real_fps < kDivergenceMaxRealFps);
}

int get_current_step_index(void) {
    return s_run.current_step_index;
}

void get_ui_snapshot(DvfsUiSnapshot* out) {
    // Acquire pairs with the release store of TEST_PHASE_COMPLETE.
    out->test_phase = (int)s_run.test_phase.load(std::memory_order_acquire);
    out->test_scenario = get_test_scenario();
    out->current_step_index = s_run.current_step_index;
    out->scenario_step_count = s_run.scenario_step_count;
    out->step_time_remaining_sec = step_time_remaining_sec();
    out->target_workload_ms = s_run.target_workload_ms;
    out->last_frame_expected_ms = s_ui.last_frame_expected_ms;
    out->last_frame_real_cpu_ms = s_ui.last_frame_real_cpu_ms;
    out->expected_fps = s_ui.expected_fps;
    out->measured_real_fps = s_ui.measured_real_fps;
    out->fps_diverged = get_is_fps_diverged();
    out->cooldown_progress_sec = s_ui.cooldown_progress_sec;
    out->cooldown_headroom_pct = s_ui.cooldown_headroom_pct;
    out->current_cpu_core = s_ui.current_cpu_core;
    out->current_cpu_cluster = s_ui.current_cpu_cluster;
    out->cpu_cluster_count = cpu_cluster_count();
    out->current_cpu_freq_mhz = s_ui.current_cpu_freq_mhz;
    out->current_cpu_max_freq_mhz = s_ui.current_cpu_max_freq_mhz;
    out->cpu_freq_ratio_pct = s_ui.cpu_freq_ratio_pct;
    out->cpu_freq_pct = s_ui.current_cpu_freq_pct;
    out->gpu_freq_pct = s_ui.current_gpu_freq_pct;
    out->calib_freq_mhz = s_ui.calib_freq_mhz;
    out->device_global_max_freq_mhz = s_device_global_max_freq_mhz;
    out->calib_at_max_freq = s_ui.calib_at_max_freq;
    // Acquire pairs with the release store in mark_run_tainted(), so the
    // reason is only ever read after it has been fully written.
    out->run_tainted = s_run_tainted.load(std::memory_order_acquire);
    out->taint_reason = out->run_tainted ? s_run.taint_reason : "";
    out->run_failed = out->run_tainted && s_run.run_failed.load(std::memory_order_relaxed);
    for (int k = 0; k < CALIBRATION_KIND_COUNT; ++k) {
        out->calibration_status[k] = s_ui.calibration_status[k];
    }
    out->missing_calibrations = s_run.missing_calibrations;
}

// Appends one frame interval to the rolling window behind s_ui.measured_real_fps.
static void record_frame_time_sample(uint64_t frame_dur_ns) {
    s_run.frame_times_ns[s_run.frame_time_idx] = frame_dur_ns;
    s_run.frame_time_idx = (s_run.frame_time_idx + 1) % kFpsHistorySize;
    if (s_run.frame_time_count < kFpsHistorySize) {
        s_run.frame_time_count++;
    }
    uint64_t total_dur_ns = 0;
    for (int i = 0; i < s_run.frame_time_count; ++i) {
        total_dur_ns += s_run.frame_times_ns[i];
    }
    if (total_dur_ns > 0) {
        s_ui.measured_real_fps = (float)s_run.frame_time_count / ((float)total_dur_ns / 1e9f);
    }
}

// The largest workload targets expected to sustain 60 and 30 FPS.
static constexpr float kMax60FpsWorkloadMs = 14.0f;
static constexpr float kMax30FpsWorkloadMs = 33.0f;

// The cadence a workload target is expected to sustain.
static float expected_fps_for_target(float target_workload_ms) {
    if (s_run.active_scenario_explicit_30fps) return 30.0f;
    if (target_workload_ms <= kMax60FpsWorkloadMs) return 60.0f;
    if (target_workload_ms <= kMax30FpsWorkloadMs) return 30.0f;
    return 20.0f;
}

// Derives the cadence the active target is expected to sustain.
static void update_expected_fps(void) {
    s_ui.expected_fps = expected_fps_for_target(s_run.target_workload_ms);
}

static int32_t compute_gpu_iterations_for_target_ms(double target_gpu_ms, double gpu_iters_per_ms,
                                                    double gpu_fixed_ms,
                                                    bool* out_clamped = nullptr) {
    if (out_clamped) *out_clamped = false;
    if (target_gpu_ms <= 0.0 || gpu_iters_per_ms <= 0.0) {
        return 0;
    }
    const double net_shader_ms = std::max(0.0, target_gpu_ms - std::max(0.0, gpu_fixed_ms));
    int64_t raw_iters = static_cast<int64_t>(std::llround(net_shader_ms * gpu_iters_per_ms));
    if (raw_iters < 1) {
        raw_iters = 1;
    }
    const int64_t max_iters = static_cast<int64_t>(samples::Circle::kMaxGpuIterations);
    if (raw_iters > max_iters) {
        if (out_clamped) *out_clamped = true;
        raw_iters = max_iters;
    }
    return static_cast<int32_t>(raw_iters);
}

// Opens the async trace slice spanning scenario step step_idx, which must
// already be staged. Called under s_test_mutex.
static void trace_step_begin(int step_idx) {
    if (!SWAPPY_TEST_TRACE_ENABLED()) return;
    const ScenarioStep* step = &s_run.scenario_steps[step_idx];
    // The same clamp dispatch_gpu_workload() applies on every frame.
    [[maybe_unused]] bool gpu_clamped = false;
    (void)compute_gpu_iterations_for_target_ms(static_cast<double>(step->target_gpu_ms),
                                               s_run.calibrated_gpu_iters_per_ms.load(
                                                       std::memory_order_relaxed),
                                               s_run.calibrated_gpu_fixed_ms.load(
                                                       std::memory_order_relaxed),
                                               &gpu_clamped);
    SWAPPY_TEST_BEGIN_STEP("SwappyTest::Step;%d;tgt=%d;cpu=%d;gpu=%d;dur=%.2f;exp_fps=%.1f;jit=%d-%"
                           "d"
                           ";gpu_clamped=%d;reset=%d;boost=%d;fps=%d;phase=%s",
                           step_idx, (int)s_run.target_workload_ms, step->target_cpu_ms,
                           step->target_gpu_ms, step->duration_sec,
                           expected_fps_for_target(s_run.target_workload_ms), step->jitter_min_ms,
                           step->jitter_max_ms, gpu_clamped ? 1 : 0,
                           step->reset_pacing_at_start ? 1 : 0, step->external_boost_pulse ? 1 : 0,
                           step->target_fps,
                           get_phase_name_for_step(step_idx, s_run.scenario_step_count));
}

// Marks the start of the run with the settings and calibration metadata the
// host scorer reads. Called under s_test_mutex on the first frame of step 0,
// once Swappy is up and the scenario is staged. Swappy traces its own
// projection and clock settings.
static void trace_run_settings(void) {
    if (!SWAPPY_TEST_TRACE_ENABLED()) return;
    // '|' separates marker fields, and a CUSTOM description may contain it.
    char desc[sizeof(s_run.selected_scenario_desc)];
    snprintf(desc, sizeof(desc), "%s", s_run.selected_scenario_desc);
    for (char* c = desc; *c != '\0'; ++c) {
        if (*c == '|') *c = '/';
    }
    SWAPPY_TEST_TRACE_INSTANT("SwappyTest::Settings|scenario=%s|api=%s|pacing=%s|iter=%d"
                              "|uses_gpu=%d|explicit_30fps=%d|square_wave=%d"
                              "|user_30fps=%d|steps=%d|refresh_ns=%llu|device_max_mhz=%d|headroom_"
                              "start=%d"
                              "|desc=%s",
                              s_run.selected_scenario_name, get_active_render_api_name(),
                              swappy_backend_get_pacing_strategy_name(), s_run.run_iteration,
                              s_active_scenario_uses_gpu ? 1 : 0,
                              s_run.active_scenario_explicit_30fps ? 1 : 0,
                              s_run.active_scenario_square_wave ? 1 : 0,
                              s_run.user_30fps_limit ? 1 : 0, s_run.scenario_step_count.load(),
                              (unsigned long long)get_display_refresh_period_ns(),
                              s_device_global_max_freq_mhz.load(), s_headroom_start_pct.load(),
                              desc);

    const int64_t gpu_pixels =
            static_cast<int64_t>(s_render_surface_width.load()) * s_render_surface_height.load();
    SWAPPY_TEST_TRACE_INSTANT("SwappyTest::Calibration|cpu_iters_per_ms=%llu|calib_core=%d|calib_"
                              "freq_mhz=%d"
                              "|cpu_mode=%s|cpu_r2=%.6f|cpu_rme95=%.2f|cpu_mean_err=%.2f|cpu_"
                              "chunks=%d"
                              "|gpu_iters_per_ms=%.4f|gpu_fixed_ms=%.4f|gpu_timer=%s|gpu_pixels="
                              "%" PRId64
                              "|gpu_r2=%.6f|gpu_rme95=%.2f|gpu_mean_err=%.2f|gpu_samples=%d",
                              (unsigned long long)s_run.calibrated_iters_per_ms.load(),
                              s_ui.calib_core.load(), s_ui.calib_freq_mhz.load(),
                              s_cpu_calibration_result.mode.c_str(),
                              s_cpu_calibration_result.r_squared,
                              s_cpu_calibration_result.rme95_pct,
                              s_cpu_calibration_result.mean_error_pct,
                              s_cpu_calibration_result.accepted_chunks,
                              s_run.calibrated_gpu_iters_per_ms.load(),
                              s_run.calibrated_gpu_fixed_ms.load(),
                              s_gpu_calibration_result.timer.c_str(), gpu_pixels,
                              s_gpu_calibration_result.r_squared,
                              s_gpu_calibration_result.rme95_pct,
                              s_gpu_calibration_result.mean_error_pct,
                              s_gpu_calibration_result.accepted_samples);
}

// Opens the trace slice of the step just staged and fires its start-of-step
// side effects. Called under s_test_mutex.
static void begin_staged_step(int step_idx, uint64_t now_ns) {
    const ScenarioStep* step = &s_run.scenario_steps[step_idx];
    trace_step_begin(step_idx);
    if (step->reset_pacing_at_start) {
        ALOGI("SwappyTest: Step %d invoking resetFramePacing()", step_idx);
        swappy_backend_reset_frame_pacing();
    }
    if (step->external_boost_pulse) {
        fire_external_boost_pulse(now_ns);
    }
}

// Drops the workload to a 1 ms CPU load with no GPU work once no step runs.
static void set_idle_targets(void) {
    s_run.target_workload_ms = 1.0f;
    s_run.target_cpu_workload_ms = 1.0f;
    s_run.target_gpu_workload_ms = 0.0f;
    s_ui.current_gpu_workload = 0;
}

// Closes the active step once its duration has elapsed, staging the next
// step or completing the run.
static void advance_scenario_step(uint64_t now_ns) {
    if (s_run.test_phase == TEST_PHASE_RUNNING) {
        uint64_t elapsed_step_ns = now_ns - s_run.current_step_start_ns;
        uint64_t target_step_ns = (uint64_t)(s_run.current_step_duration_sec * 1000000000.0);

        if (elapsed_step_ns >= target_step_ns) {
            SWAPPY_TEST_END_STEP();
            s_run.current_step_start_ns = now_ns;
            s_run.current_step_index++;
            if (s_run.current_step_index < s_run.scenario_step_count) {
                SWAPPY_TEST_UPDATE_PHASE(get_phase_name_for_step(s_run.current_step_index,
                                                                 s_run.scenario_step_count));
                ScenarioStep* next = &s_run.scenario_steps[s_run.current_step_index];
                stage_scenario_step(next);
                begin_staged_step(s_run.current_step_index, now_ns);
                ALOGI("SwappyTest: [%s] Step %d/%d -> CPU=%.0fms GPU=%.0fms (dur=%.1fs, "
                      "Expected=%.1f, Real=%.1f)",
                      s_run.selected_scenario_name, s_run.current_step_index + 1,
                      s_run.scenario_step_count.load(), s_run.target_cpu_workload_ms.load(),
                      s_run.target_gpu_workload_ms.load(), s_run.current_step_duration_sec.load(),
                      s_ui.expected_fps.load(), s_ui.measured_real_fps.load());
            } else {
                set_idle_targets();
                complete_scenario_run();
                // Release-publishes the run state to the UI thread, which
                // reads it lock-free once get_ui_snapshot() reports COMPLETE.
                s_run.test_phase.store(TEST_PHASE_COMPLETE, std::memory_order_release);
                ALOGI("SwappyTest: Scenario %s completed (%d steps)! Score it on the host from "
                      "its trace.",
                      s_run.selected_scenario_name, s_run.scenario_step_count.load());
            }
        }
    } else if (s_run.test_phase == TEST_PHASE_COMPLETE || s_run.test_phase == TEST_PHASE_READY) {
        set_idle_targets();
    }
}

// Picks this frame's CPU target, applying the active step's jitter window.
static float pick_frame_cpu_workload_ms(void) {
    float frame_cpu_ms = s_run.target_cpu_workload_ms;
    if (s_run.current_step_index < s_run.scenario_step_count) {
        ScenarioStep* cur = &s_run.scenario_steps[s_run.current_step_index];
        if (cur->jitter_max_ms > cur->jitter_min_ms) {
            uint32_t span = (uint32_t)(cur->jitter_max_ms - cur->jitter_min_ms + 1);
            frame_cpu_ms = (float)(cur->jitter_min_ms + (int)(next_rng_u32() % span));
        }
    }
    return frame_cpu_ms;
}

// Publishes this frame's fragment shader iteration count and samples the GPU
// time Swappy reported for the previous frame.
static void dispatch_gpu_workload(void) {
    SWAPPY_TEST_TRACE_GPU_SUBMIT(s_run.run_frame_count);

    const int32_t gpu_workload =
            compute_gpu_iterations_for_target_ms(static_cast<double>(
                                                         s_run.target_gpu_workload_ms.load(
                                                                 std::memory_order_relaxed)),
                                                 s_run.calibrated_gpu_iters_per_ms.load(
                                                         std::memory_order_relaxed),
                                                 s_run.calibrated_gpu_fixed_ms.load(
                                                         std::memory_order_relaxed));
    if (gpu_workload > 0) {
        s_ui.current_gpu_workload = gpu_workload;
        int64_t raw_gpu_ns = s_ui.last_swappy_gpu_time_ns;
        if (raw_gpu_ns > 0) {
            s_ui.actual_gpu_workload_ms = (float)raw_gpu_ns / 1000000.0f;
        }
    } else {
        s_ui.current_gpu_workload = 0;
        s_ui.actual_gpu_workload_ms = 0.0f;
    }
}

// Burns the calibrated iteration count for frame_cpu_ms and updates the UI
// workload telemetry.
static void execute_frame_cpu_workload(float frame_cpu_ms) {
    SWAPPY_TEST_TRACE_CPU_WORK(s_run.run_frame_count);

    uint64_t cpu_dur_ns = 0;
    if (frame_cpu_ms > 0.0f) {
        uint64_t iters_to_run = (uint64_t)(frame_cpu_ms * (float)s_run.calibrated_iters_per_ms);
        uint64_t t0 = get_time_monotonic_ns();
        execute_cpu_workload(iters_to_run);
        uint64_t t1 = get_time_monotonic_ns();
        cpu_dur_ns = t1 - t0;
    }
    float measured_cpu_ms = (float)cpu_dur_ns / 1000000.0f;
    const float target_gpu_ms = s_run.target_gpu_workload_ms;
    const float actual_gpu_ms = s_ui.actual_gpu_workload_ms;
    s_ui.last_frame_expected_ms = (target_gpu_ms > frame_cpu_ms) ? target_gpu_ms : frame_cpu_ms;
    s_ui.last_frame_real_cpu_ms = measured_cpu_ms;
    s_ui.actual_workload_ms = (actual_gpu_ms > measured_cpu_ms) ? actual_gpu_ms : measured_cpu_ms;
}

// Samples the core the render thread is on and its current frequency.
static void sample_cpu_frequency(void) {
    int cpu = sched_getcpu();
    s_ui.current_cpu_core = cpu;
    if (cpu < 0) {
        return;
    }
    int cur_khz = read_cpu_freq_khz(cpu);
    int max_khz = read_cpuinfo_max_freq_khz(cpu);
    s_ui.current_cpu_cluster = cpu_cluster_of(cpu);
    s_ui.current_cpu_freq_mhz = khz_to_mhz(cur_khz);
    s_ui.current_cpu_max_freq_mhz = khz_to_mhz(max_khz);

    if (max_khz > 0 && cur_khz > 0) {
        s_ui.current_cpu_freq_pct = clamped_pct((float)cur_khz, (float)max_khz);
    } else {
        s_ui.current_cpu_freq_pct = 0.0f;
    }
}

static void sample_gpu_frequency(void) {
    int gpu_khz = read_gpu_freq_khz();
    if (gpu_khz > 0) {
        int gpu_max_khz = read_gpu_max_freq_khz();
        if (gpu_max_khz > 0) {
            s_ui.current_gpu_freq_pct = clamped_pct((float)gpu_khz, (float)gpu_max_khz);
        } else {
            s_ui.current_gpu_freq_pct = 0.0f;
        }
    } else {
        s_ui.current_gpu_freq_pct = -1.0f;
    }
}

// Scans the CPU frequency ceilings once every kHeadroomPollFrames calls.
// Returns the worst headroom percentage on those calls and -1 on all others.
static constexpr uint32_t kHeadroomPollFrames = 60;
static int poll_cpu_headroom_pct(void) {
    static uint32_t throttle_check_countdown = 0;
    if (throttle_check_countdown > 0) {
        --throttle_check_countdown;
        return -1;
    }
    throttle_check_countdown = kHeadroomPollFrames;
    return worst_cpu_headroom_pct();
}

// Taints the run if thermal mitigation lowered the CPU frequency ceiling
// while the scenario steps run. A negative headroom_pct means no fresh sample.
static void check_thermal_throttling(int headroom_pct) {
    if (headroom_pct >= 0 && headroom_pct < kThermalHeadroomOkPct) {
        char reason[192];
        snprintf(reason, sizeof(reason),
                 "thermal mitigation during the run (CPU headroom fell to %d%% at %.0f ms "
                 "target)",
                 headroom_pct, (double)s_run.target_workload_ms);
        mark_run_tainted(reason);
    }
}

// The run state is touched only under s_test_mutex, in two short scopes; the
// CPU workload and the sysfs reads in between run unlocked so no other thread
// ever waits behind them.
static constexpr uint64_t kTelemetryLogIntervalNs = 1000000000ULL;

void run_frame_swappy_test_workload(void) {
    uint64_t now_ns = get_time_monotonic_ns();
    bool steps_active = false;
    bool running_steps = false;
    float frame_cpu_ms = 0.0f;
    std::unique_lock<std::mutex> lock(s_test_mutex);
    steps_active = s_run.steps_active.load(std::memory_order_acquire);
    if (s_run.last_frame_stamp_ns > 0) {
        uint64_t frame_dur_ns = now_ns - s_run.last_frame_stamp_ns;
        record_frame_time_sample(frame_dur_ns);
        if (steps_active && s_run.test_phase != TEST_PHASE_COMPLETE &&
            frame_dur_ns < kMaxRunFrameIntervalNs) {
            ++s_run.run_frame_count;
        }
    }
    s_run.last_frame_stamp_ns = now_ns;

    SWAPPY_TEST_TRACE_FRAME(s_run.run_frame_count);

    if (steps_active) {
        if (s_run.current_step_start_ns == 0 && s_run.scenario_step_count > 0) {
            s_run.current_step_start_ns = now_ns;
            stage_scenario_step(&s_run.scenario_steps[0]);
            trace_run_settings();
            begin_staged_step(0, now_ns);
        }

        update_expected_fps();
        advance_scenario_step(now_ns);
        update_expected_fps();

        frame_cpu_ms = pick_frame_cpu_workload_ms();
        dispatch_gpu_workload();
    } else {
        s_run.target_workload_ms = 0.0f;
        s_ui.actual_workload_ms = 0.0f;
        s_ui.last_frame_expected_ms = 0.0f;
        s_ui.last_frame_real_cpu_ms = 0.0f;
        s_ui.expected_fps = 60.0f;
        s_ui.current_gpu_workload = 0;
    }
    running_steps = steps_active && s_run.test_phase != TEST_PHASE_COMPLETE;
    lock.unlock();

    if (steps_active) {
        execute_frame_cpu_workload(frame_cpu_ms);
    }
    if (!s_calibration_running) {
        sample_cpu_frequency();
    }
    sample_gpu_frequency();
    const int headroom_pct = (running_steps && !s_run_tainted) ? poll_cpu_headroom_pct() : -1;

    {
        std::lock_guard<std::mutex> re_lock(s_test_mutex);
        // Re-checked under the lock: stopping or restarting the run during the
        // unlocked workload above must neither pollute nor taint the new run.
        if (running_steps && s_run.steps_active) {
            check_thermal_throttling(headroom_pct);
            dvfs::trace_cpu_headroom_pct(headroom_pct, /*force=*/false);
        }
    }

    float freq_ratio_pct = 100.0f;
    if (s_ui.actual_workload_ms > 0.0f && s_run.target_workload_ms > 0.0f) {
        freq_ratio_pct = clamped_pct(s_run.target_workload_ms, s_ui.actual_workload_ms);
    }
    s_ui.cpu_freq_ratio_pct = freq_ratio_pct;

    static uint64_t s_last_telemetry_log_ns = 0;
    if (now_ns - s_last_telemetry_log_ns >= kTelemetryLogIntervalNs) {
        s_last_telemetry_log_ns = now_ns;
        uint64_t refresh_ns = get_display_refresh_period_ns();
        double target_fps = refresh_ns > 0 ? (1e9 / (double)refresh_ns) : 0.0;
        const char* api_label =
                (get_active_render_api() == SWAPPY_RENDER_API_VULKAN) ? "VK" : "GLES";
        ALOGI("API: %s | Expected FPS: %.1f | Real FPS: %.1f | Swappy Target FPS: %.1f | CPU: Core "
              "%d @ %d MHz (eff: %.0f%%) | Calib: Core %d @ %d/%d MHz (MaxOK: %s) | Target: %.0f "
              "ms | Actual: %.1f ms | Step: %.1fs (rem: %.1fs) | Diverged: %s | Tainted: %s | "
              "Phase: %d | Scenario: %s",
              api_label, (double)s_ui.expected_fps, (double)s_ui.measured_real_fps, target_fps,
              (int)s_ui.current_cpu_core, (int)s_ui.current_cpu_freq_mhz, (double)freq_ratio_pct,
              (int)s_ui.calib_core, (int)s_ui.calib_freq_mhz, (int)s_device_global_max_freq_mhz,
              s_ui.calib_at_max_freq ? "true" : "false", (double)s_run.target_workload_ms,
              (double)s_ui.actual_workload_ms, (double)step_duration_sec(),
              (double)step_time_remaining_sec(), get_is_fps_diverged() ? "true" : "false",
              s_run_tainted ? "true" : "false",
              (int)s_run.test_phase.load(std::memory_order_relaxed), s_run.selected_scenario_name);
    }
}
