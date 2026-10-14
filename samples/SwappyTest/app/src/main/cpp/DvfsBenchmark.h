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

#include <stdbool.h>
#include <stdint.h>

#include "CalibrationKind.h"

#ifdef __cplusplus
extern "C" {
#endif

// Phase of the current test. SwappyTestActivity mirrors these values.
typedef enum {
    TEST_PHASE_READY = -1,
    TEST_PHASE_RUNNING = 1,
    TEST_PHASE_COMPLETE = 4,
    TEST_PHASE_COOLDOWN = 5
} TestPhase;

// Graphics API backend used by SwappyTestRenderer. SwappyTestActivity mirrors these values.
typedef enum { SWAPPY_RENDER_API_GLES = 0, SWAPPY_RENDER_API_VULKAN = 1 } SwappyRenderApi;

void set_active_render_api(SwappyRenderApi api);
SwappyRenderApi get_active_render_api(void);
const char* get_active_render_api_name(void);

// Backend-agnostic Swappy dispatch hooks implemented by SwappyTestRenderer.cpp.
uint64_t swappy_backend_get_refresh_period_ns(void);
void swappy_backend_set_swap_interval_ns(uint64_t swap_ns);
void swappy_backend_set_auto_swap_interval(bool enabled);
void swappy_backend_set_auto_pipeline_mode(bool enabled);
void swappy_backend_set_buffer_stuffing_fix_wait(int32_t n_frames);
// The options the app last applied, which a scenario run starts from; the
// stuffing fix wait is -1 while never set.
bool swappy_backend_get_auto_pipeline_mode(void);
int32_t swappy_backend_get_buffer_stuffing_fix_wait(void);
void swappy_backend_reset_frame_pacing(void);
const char* swappy_backend_get_pacing_strategy_name(void);

void start_benchmark_run(void);
void stop_benchmark_run(void);
void run_frame_swappy_test_workload(void);
void record_swappy_post_wait_sample(int64_t cpu_time_ns, int64_t gpu_time_ns);
void set_user_30fps_limit(bool enabled);

// What the overlay HUD and SwappyTestActivity display. Each field is read
// lock-free on its own, so one snapshot may mix values from adjacent frames.
typedef struct {
    // A TestPhase. Read with acquire order, so a snapshot that reports
    // TEST_PHASE_COMPLETE also sees the run record written before it.
    int test_phase;
    // The selected scenario id; valid until the calling thread's next
    // get_ui_snapshot().
    const char* test_scenario;
    int current_step_index;
    int scenario_step_count;
    float step_elapsed_sec;
    float target_workload_ms;
    float last_frame_expected_ms;
    float last_frame_real_cpu_ms;
    float measured_real_fps;
    float cooldown_progress_sec;
    int cooldown_headroom_pct;
    int current_cpu_core;
    int current_cpu_cluster;
    int cpu_cluster_count;
    int current_cpu_freq_mhz;
    int current_cpu_max_freq_mhz;
    float cpu_freq_ratio_pct;
    float cpu_freq_pct;
    float gpu_freq_pct;
    int calib_freq_mhz;
    int device_global_max_freq_mhz;
    bool calib_at_max_freq;
    bool run_tainted;
    // "" unless run_tainted; never rewritten once set.
    const char* taint_reason;
    // The run failed and produced no result; taint_reason says why.
    bool run_failed;
    // The CalibrationStatus of each CalibrationKind's cache, as of the last
    // refresh_calibration_status() or run start.
    int calibration_status[CALIBRATION_KIND_COUNT];
    // The calibrations the last scenario run needed but found missing or
    // invalid; 0 unless it failed for want of them.
    CalibrationSet missing_calibrations;
} DvfsUiSnapshot;

void get_ui_snapshot(DvfsUiSnapshot* out);

int get_current_step_index(void);

// Scenario engine & run record (T7, T8, T9)
void set_test_scenario(const char* scenario_name);
void select_scenario(int index);
int get_scenario_count(void);
const char* get_scenario_id_by_index(int index);
const char* get_scenario_description_by_index(int index);
void set_run_iteration(int iteration);
void set_output_directory(const char* dir_path);
// A non-empty kinds makes the next run calibrate those kinds and store each
// passing one in its own cache instead of running the selected scenario. The
// GPU kind must be the active render API's. A scenario run fails unless the
// caches of the calibrations its steps need are valid.
void set_calibration_run(CalibrationSet kinds);
// Rereads the calibration caches into get_ui_snapshot()'s calibration_status.
void refresh_calibration_status(void);
int32_t get_current_gpu_workload(void);
bool is_scenario_explicit_30fps(void);
float get_last_swappy_gpu_ms(void);
void set_render_surface_size(int32_t width, int32_t height);

#ifdef __cplusplus
}

#include "Scenario.h"

void set_test_scenario(dvfs::Scenario scenario);

namespace samples {
class GpuCalibrationTarget;
}

enum class CalibrationHoldAction {
    kNone,
    kDrainAndAcknowledge,
    kRunGpuCalibration,
    kIdle,
};

// Called once per render-loop iteration before any frame work.
// kIdle returns after a brief sleep (e.g. 5 ms) so the render thread remains responsive.
CalibrationHoldAction poll_calibration_hold(void);
void acknowledge_calibration_hold(void);
void run_gpu_calibration(samples::GpuCalibrationTarget& target);

#endif
