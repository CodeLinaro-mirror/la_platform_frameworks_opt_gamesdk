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

#include "Scenario.h"

#define LOG_TAG "SwappyTest"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <iterator>

#include "Log.h"

namespace dvfs {

// S1 and S7 hold the 27 ms spike, then one 18 ms step that cannot fit a 60 FPS
// frame at any clock, then step down from 14 ms to 4 ms, where 60 FPS should
// come back.
static const int RAMP_DOWN_TARGETS_MS[] = {27, 18, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4};

// S2 climbs through the targets around the 16.7 ms budget: 12 and 14 ms fit a
// 60 FPS frame, 15-17 ms may or may not, and 18 ms and up cannot.
static const int RAMP_UP_TARGETS_MS[] = {12, 14, 15, 16, 17, 18, 20, 27};

// One duration_sec CPU step per target, with no GPU work.
template <size_t N>
static std::vector<ScenarioSegment> cpu_ramp(const int (&targets_ms)[N], float duration_sec) {
    std::vector<ScenarioSegment> segments;
    for (int ms : targets_ms) segments.push_back({1, ms, 0, duration_sec});
    return segments;
}

// One duration_sec GPU step per target, each with a 1 ms CPU load.
template <size_t N>
static std::vector<ScenarioSegment> gpu_ramp(const int (&targets_ms)[N], float duration_sec) {
    std::vector<ScenarioSegment> segments;
    for (int ms : targets_ms) segments.push_back({1, 1, ms, duration_sec});
    return segments;
}

// Four periods cover the projected ladder's first three upshift attempts,
// about 2, 12 and 30 s after the first light half-period starts; the doubling
// backoff puts a fourth past 60 s.
static std::vector<ScenarioSegment> square_wave_segments(void) {
    std::vector<ScenarioSegment> segments;
    for (int i = 0; i < 4; ++i) {
        segments.push_back({1, 27, 0, 5.0f});
        segments.push_back({1, 10, 0, 5.0f});
    }
    return segments;
}

// The scenario catalog, in UI order. S1 is the default. S3 and H3, and S5
// and H7, are the same scenario under two ids; S3 is kept as an on-demand
// alias and the default matrix runs H3.
static const ScenarioDef kScenarios[] = {
        {"S1", "S1: Deterministic 39.0s CPU Ramp-Down (27, 18, 14->4 ms)",
         cpu_ramp(RAMP_DOWN_TARGETS_MS, 3.0f), 0},
        {"S2", "S2: CPU Ramp-Up 12->27 ms (Downshift Ladder Regression, 24.0s)",
         cpu_ramp(RAMP_UP_TARGETS_MS, 3.0f), 0},
        {"S3", "S3: Honeytrap Square Wave 27 ms <-> 10 ms (5s half-period, 40s)",
         square_wave_segments(), kScenarioSquareWave},
        {"S4",
         "S4: Uniform Random 6-14 ms In-Budget Jitter (30.0s)",
         {{6, 10, 0, 5.0f, /*jitter_min_ms=*/6, /*jitter_max_ms=*/14}},
         0},
        {"S5",
         "S5: Explicit 30 FPS App Request (8 ms load, 10.0s)",
         {{2, 8, 0, 5.0f}},
         kScenarioExplicit30Fps},
        {"S7", "S7: GPU Ramp-Down (27, 18, 14->4 ms GPU, 1 ms CPU, 39.0s)",
         gpu_ramp(RAMP_DOWN_TARGETS_MS, 3.0f), 0},
        {"S10",
         "S10: resetFramePacing() Recovery (27ms -> 12ms -> 27ms -> 12ms reset, 18.0s)",
         {{1, 27, 0, 3.0f},
          {1, 12, 0, 6.0f},
          {1, 27, 0, 3.0f},
          {1, 12, 0, 6.0f, 0, 0, /*reset_pacing=*/true}},
         0},
        {"S11", "S11: 10-Minute Heavy Load Soak (27 ms x 600s)", {{20, 27, 0, 30.0f}}, 0},
        {"H1", "H1: Honeytrap Constant 27 ms Heavy Load (36.0s)", {{6, 27, 0, 6.0f}}, 0},
        {"H2",
         "H2: Honeytrap Marginal 18 ms (24s) then 14 ms Mirror (24s)",
         {{4, 18, 0, 6.0f}, {4, 14, 0, 6.0f}},
         0},
        {"H3", "H3: Honeytrap Square Wave 27 ms <-> 10 ms (5s half-period, 40s)",
         square_wave_segments(), kScenarioSquareWave},
        {"H4",
         "H4: Honeytrap Uniform Random 20-30 ms (36.0s)",
         {{6, 25, 0, 6.0f, /*jitter_min_ms=*/20, /*jitter_max_ms=*/30}},
         0},
        {"H6",
         "H6: Honeytrap GPU-Bound 25 ms Fragment Shader + 1 ms CPU (36.0s)",
         {{6, 1, 25, 6.0f}},
         0},
        {"H7",
         "H7: Explicit 30 FPS App Request (8 ms load, 10.0s)",
         {{2, 8, 0, 5.0f}},
         kScenarioExplicit30Fps},
        {"H8", "H8: Honeytrap Idle App at Full Rate (1 ms load, 12.0s)", {{2, 1, 0, 6.0f}}, 0},
        {"H9",
         "H9: Bottleneck Flip CPU-Heavy (24ms) -> GPU-Heavy (24ms) -> GPU-Light (10ms)",
         {{2, 24, 0, 5.0f}, {2, 1, 24, 5.0f}, {2, 1, 10, 5.0f}},
         0},
        {"H10",
         "H10: Honeytrap 22 ms Load + 500 ms External Big-Core Boost Every 6s (36.0s)",
         {{6, 22, 0, 6.0f, 0, 0, false, /*boost_pulse=*/true}},
         0},
        {"CUSTOM", "CUSTOM: User-Defined Steps (configured via intent or UI)", {}, kScenarioCustom},
};

const ScenarioDef* find_scenario(const char* id, bool ignore_case) {
    if (!id) return nullptr;
    for (const ScenarioDef& def : kScenarios) {
        if ((ignore_case ? strcasecmp(id, def.id) : strcmp(id, def.id)) == 0) return &def;
    }
    return nullptr;
}

int scenario_count(void) {
    return static_cast<int>(std::size(kScenarios));
}

const ScenarioDef& scenario_at(int index) {
    return kScenarios[index];
}

static ScenarioStep* add_step_ex(std::vector<ScenarioStep>* steps, int cpu_ms, int gpu_ms,
                                 float dur_sec, int jmin, int jmax, bool reset_pacing,
                                 bool boost_pulse) {
    if (static_cast<int>(steps->size()) >= kMaxScenarioSteps) {
        ALOGE("SwappyTest: Maximum step count reached (%d), dropping step!", kMaxScenarioSteps);
        return nullptr;
    }
    ScenarioStep* st = &steps->emplace_back();
    st->target_cpu_ms = cpu_ms;
    st->target_gpu_ms = gpu_ms;
    st->duration_sec = dur_sec;
    st->jitter_min_ms = jmin;
    st->jitter_max_ms = jmax;
    st->reset_pacing_at_start = reset_pacing;
    st->external_boost_pulse = boost_pulse;
    st->auto_pipeline = -1;
    st->auto_swap_interval = -1;
    st->buffer_stuffing_fix_wait = -1;
    st->target_fps = 0;
    return st;
}

static void add_step(std::vector<ScenarioStep>* steps, int cpu_ms, int gpu_ms, float dur_sec) {
    add_step_ex(steps, cpu_ms, gpu_ms, dur_sec, 0, 0, false, false);
}

static bool parse_bool_token(const char* val, int8_t* out_val) {
    if (!val || val[0] == '\0') return false;
    if (strcasecmp(val, "on") == 0 || strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0) {
        *out_val = 1;
        return true;
    }
    if (strcasecmp(val, "off") == 0 || strcasecmp(val, "false") == 0 || strcmp(val, "0") == 0) {
        *out_val = 0;
        return true;
    }
    return false;
}

static bool parse_pipeline_token(const char* val, int8_t* out_val) {
    if (!val || val[0] == '\0') return false;
    if (strcasecmp(val, "auto") == 0) {
        *out_val = 2;
        return true;
    }
    return parse_bool_token(val, out_val);
}

std::vector<ScenarioStep> parse_custom_steps(const char* spec) {
    std::vector<ScenarioStep> steps;
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", spec ? spec : "");

    char* save_step = nullptr;
    char* step_tok = strtok_r(buf, ";|", &save_step);
    while (step_tok != nullptr) {
        while (*step_tok == ' ' || *step_tok == '\t') ++step_tok;
        if (*step_tok != '\0') {
            float dur_sec = 5.0f;
            int cpu_ms = 0;
            int gpu_ms = 0;
            int jmin = 0;
            int jmax = 0;
            bool reset_pacing = false;
            bool boost_pulse = false;
            int8_t auto_pipe = -1;
            int8_t auto_si = -1;
            int32_t stuff_fix = -1;
            int32_t target_fps = 0;

            // Optional "<dur>[s]:" prefix before key=value list
            char* colon = strchr(step_tok, ':');
            char* kv_list = step_tok;
            if (colon != nullptr) {
                *colon = '\0';
                char* end_dur = nullptr;
                float parsed_dur = strtof(step_tok, &end_dur);
                if (end_dur != step_tok && parsed_dur > 0.0f) {
                    dur_sec = parsed_dur;
                }
                kv_list = colon + 1;
            }

            char* save_kv = nullptr;
            char* kv = strtok_r(kv_list, ",", &save_kv);
            while (kv != nullptr) {
                while (*kv == ' ' || *kv == '\t') ++kv;
                char* eq = strchr(kv, '=');
                if (eq != nullptr) {
                    *eq = '\0';
                    const char* k = kv;
                    const char* v = eq + 1;
                    while (*v == ' ' || *v == '\t') ++v;
                    // Strip trailing whitespace from key and value
                    char* k_end = eq - 1;
                    while (k_end >= k && (*k_end == ' ' || *k_end == '\t')) *k_end-- = '\0';
                    char v_clean[64];
                    snprintf(v_clean, sizeof(v_clean), "%s", v);
                    size_t v_len = strlen(v_clean);
                    while (v_len > 0 && (v_clean[v_len - 1] == ' ' || v_clean[v_len - 1] == '\t')) {
                        v_clean[--v_len] = '\0';
                    }

                    if (strcasecmp(k, "dur") == 0 || strcasecmp(k, "d") == 0 ||
                        strcasecmp(k, "duration") == 0) {
                        float d = strtof(v_clean, nullptr);
                        if (d > 0.0f) dur_sec = d;
                    } else if (strcasecmp(k, "cpu") == 0 || strcasecmp(k, "cpu_ms") == 0) {
                        int c = atoi(v_clean);
                        cpu_ms = c >= 0 ? c : 0;
                    } else if (strcasecmp(k, "gpu") == 0 || strcasecmp(k, "gpu_ms") == 0) {
                        int g = atoi(v_clean);
                        gpu_ms = g >= 0 ? g : 0;
                    } else if (strcasecmp(k, "jit") == 0 || strcasecmp(k, "jitter") == 0) {
                        int lo = 0, hi = 0;
                        if (sscanf(v_clean, "%d-%d", &lo, &hi) == 2 && lo >= 0 && hi >= lo) {
                            jmin = lo;
                            jmax = hi;
                        }
                    } else if (strcasecmp(k, "pipe") == 0 || strcasecmp(k, "pipeline") == 0 ||
                               strcasecmp(k, "auto_pipeline") == 0) {
                        parse_pipeline_token(v_clean, &auto_pipe);
                    } else if (strcasecmp(k, "auto_si") == 0 ||
                               strcasecmp(k, "auto_swap_interval") == 0) {
                        parse_bool_token(v_clean, &auto_si);
                    } else if (strcasecmp(k, "stuff_fix") == 0 ||
                               strcasecmp(k, "buffer_stuffing_fix") == 0) {
                        int sf = atoi(v_clean);
                        if (sf >= 0) stuff_fix = sf;
                    } else if (strcasecmp(k, "fps") == 0) {
                        int f = atoi(v_clean);
                        if (f > 0) target_fps = f;
                    } else if (strcasecmp(k, "reset") == 0) {
                        int8_t b = 0;
                        if (parse_bool_token(v_clean, &b)) reset_pacing = (b != 0);
                    } else if (strcasecmp(k, "boost") == 0) {
                        int8_t b = 0;
                        if (parse_bool_token(v_clean, &b)) boost_pulse = (b != 0);
                    } else {
                        ALOGW("SwappyTest: Ignoring unknown custom step key '%s'", k);
                    }
                }
                kv = strtok_r(nullptr, ",", &save_kv);
            }

            ScenarioStep* st = add_step_ex(&steps, cpu_ms, gpu_ms, dur_sec, jmin, jmax,
                                           reset_pacing, boost_pulse);
            if (st != nullptr) {
                st->auto_pipeline = auto_pipe;
                st->auto_swap_interval = auto_si;
                st->buffer_stuffing_fix_wait = stuff_fix;
                st->target_fps = target_fps;
            }
        }
        step_tok = strtok_r(nullptr, ";|", &save_step);
    }

    if (steps.empty()) {
        add_step(&steps, 0, 0, 5.0f);
    }
    return steps;
}

static void add_segments(std::vector<ScenarioStep>* steps,
                         const std::vector<ScenarioSegment>& segments) {
    for (const ScenarioSegment& seg : segments) {
        for (int i = 0; i < seg.repeat; ++i) {
            add_step_ex(steps, seg.cpu_ms, seg.gpu_ms, seg.duration_sec, seg.jitter_min_ms,
                        seg.jitter_max_ms, seg.reset_pacing, seg.boost_pulse);
        }
    }
}

std::vector<ScenarioStep> build_scenario_steps(const ScenarioDef& def, const char* custom_spec) {
    if (def.flags & kScenarioCustom) return parse_custom_steps(custom_spec);
    std::vector<ScenarioStep> steps;
    add_segments(&steps, def.segments);
    return steps;
}

ScenarioTraits scenario_traits(const ScenarioDef& def, const std::vector<ScenarioStep>& steps) {
    ScenarioTraits traits = {};
    traits.explicit_30fps = (def.flags & kScenarioExplicit30Fps) != 0;
    traits.square_wave = (def.flags & kScenarioSquareWave) != 0;
    for (const ScenarioStep& step : steps) {
        traits.uses_cpu |= step.target_cpu_ms > 0 || step.jitter_max_ms > 0;
        traits.uses_gpu |= step.target_gpu_ms > 0;
        traits.explicit_30fps |= step.target_fps > 0 && step.target_fps <= 30;
        traits.needs_boost |= step.external_boost_pulse;
    }
    return traits;
}

CalibrationSet required_calibrations(const ScenarioTraits& traits, CalibrationKind render_api) {
    CalibrationSet required = 0;
    if (traits.uses_cpu) required |= calibration_bit(CALIBRATION_CPU);
    if (traits.uses_gpu) required |= calibration_bit(render_api);
    return required;
}

} // namespace dvfs
