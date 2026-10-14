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

// Helpers the scenario host tests share: they play scenarios on a fake clock.

#pragma once

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Scenario.h"
#include "ScenarioBuilder.h"
#include "ScenarioEngine.h"

namespace dvfs {

// The fake clock advances a round 10 ms per frame: 100 frames per second.
inline constexpr uint64_t kFakeFrameNs = 10000000ULL;
inline constexpr uint64_t kFakeStartNs = 1000ULL * 1000000000ULL;

// Finishes builder, which must hold a valid scenario, and returns the scenario.
inline Scenario built(ScenarioBuilder& builder) {
    EXPECT_TRUE(builder.finish()) << builder.error();
    return builder.ok() ? builder.build() : Scenario();
}

// Plays scenario on the fake clock until it finishes or max_frames frames.
inline std::vector<ScenarioEngine::Frame> play(const Scenario& scenario,
                                               size_t max_frames = 100000) {
    ScenarioEngine engine;
    engine.start(scenario);
    std::vector<ScenarioEngine::Frame> frames;
    uint64_t now_ns = kFakeStartNs;
    while (frames.size() < max_frames) {
        const ScenarioEngine::Frame frame = engine.next_frame(now_ns);
        if (frame.finished) break;
        frames.push_back(frame);
        now_ns += kFakeFrameNs;
    }
    return frames;
}

inline std::vector<ScenarioEngine::Frame> play(ScenarioBuilder& builder) {
    return play(built(builder));
}

inline std::vector<double> cpu_sequence(const std::vector<ScenarioEngine::Frame>& frames) {
    std::vector<double> cpu_ms;
    for (const ScenarioEngine::Frame& frame : frames) {
        cpu_ms.push_back(frame.cpu_ms);
    }
    return cpu_ms;
}

inline std::vector<double> cpu_sequence(const Scenario& scenario, size_t max_frames = 100000) {
    return cpu_sequence(play(scenario, max_frames));
}

inline std::vector<double> cpu_sequence(ScenarioBuilder& builder) {
    return cpu_sequence(built(builder));
}

inline std::vector<double> gpu_sequence(const Scenario& scenario, size_t max_frames = 100000) {
    std::vector<double> gpu_ms;
    for (const ScenarioEngine::Frame& frame : play(scenario, max_frames)) {
        gpu_ms.push_back(frame.gpu_ms);
    }
    return gpu_ms;
}

// The frame indices at which a step begins: the step index changes there.
inline std::vector<size_t> step_starts(const std::vector<ScenarioEngine::Frame>& frames) {
    std::vector<size_t> starts;
    for (size_t i = 0; i < frames.size(); ++i) {
        if (i == 0 || frames[i].step_index != frames[i - 1].step_index) starts.push_back(i);
    }
    return starts;
}

// The frames the fake clock plays in seconds.
inline size_t frames_in_seconds(double seconds) {
    return static_cast<size_t>(std::llround(seconds * 1e9 / static_cast<double>(kFakeFrameNs)));
}

} // namespace dvfs
