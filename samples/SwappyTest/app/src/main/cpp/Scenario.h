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

// The DvfsBenchmark scenario catalog and the custom step parser. Pure: the
// caller applies the steps and traits to the run state.

#include <stdint.h>

#include <vector>

#include "CalibrationKind.h"

namespace dvfs {

constexpr int kMaxScenarioSteps = 32;

struct ScenarioStep {
    int target_cpu_ms;
    int target_gpu_ms;
    float duration_sec;
    int jitter_min_ms;
    int jitter_max_ms;
    bool reset_pacing_at_start;
    bool external_boost_pulse;
    int8_t auto_pipeline;             // -1 = unchanged, 0 = off, 1 = on, 2 = auto
    int8_t auto_swap_interval;        // -1 = unchanged, 0 = off, 1 = on
    int32_t buffer_stuffing_fix_wait; // -1 = unchanged, >= 0 = n_frames
    int32_t target_fps;               // 0 = unchanged, > 0 = explicit FPS
};

// A run of repeat identical scenario steps.
struct ScenarioSegment {
    int repeat;
    int cpu_ms;
    int gpu_ms;
    float duration_sec;
    int jitter_min_ms = 0;
    int jitter_max_ms = 0;
    bool reset_pacing = false;
    bool boost_pulse = false;
};

enum ScenarioFlags : uint8_t {
    kScenarioExplicit30Fps = 1 << 0,
    kScenarioSquareWave = 1 << 1,
    // Steps come from the custom steps spec instead of segments.
    kScenarioCustom = 1 << 2,
};

struct ScenarioDef {
    const char* id;
    const char* description;
    std::vector<ScenarioSegment> segments;
    uint8_t flags;
};

// The scenario catalog, in UI order. Entry 0, S1, is the default.
int scenario_count(void);
const ScenarioDef& scenario_at(int index);
const ScenarioDef* find_scenario(const char* id, bool ignore_case);

// Parses a semicolon-separated custom step specification such as:
//   "3s:cpu=0,gpu=0; 5s:cpu=8,gpu=0,pipe=off; 5s:cpu=4,gpu=12,pipe=on,stuff_fix=2"
// Never empty: an empty spec yields one idle 5 s step. At most
// kMaxScenarioSteps steps.
std::vector<ScenarioStep> parse_custom_steps(const char* spec);

// The steps of def; a custom scenario parses custom_spec.
std::vector<ScenarioStep> build_scenario_steps(const ScenarioDef& def, const char* custom_spec);

// What a scenario's steps need from the run.
struct ScenarioTraits {
    // Some step has a calibrated CPU load.
    bool uses_cpu;
    // Some step has a calibrated GPU load.
    bool uses_gpu;
    // An explicit FPS of 30 or less.
    bool explicit_30fps;
    bool square_wave;
    // Boost pulses need the boost thread.
    bool needs_boost;
};

ScenarioTraits scenario_traits(const ScenarioDef& def, const std::vector<ScenarioStep>& steps);

// The calibrations a scenario with traits needs on render_api: a CPU load
// needs the CPU calibration, a GPU load the GPU calibration of render_api.
// Boost pulses burn a fixed iteration count and need none.
CalibrationSet required_calibrations(const ScenarioTraits& traits, CalibrationKind render_api);

} // namespace dvfs
