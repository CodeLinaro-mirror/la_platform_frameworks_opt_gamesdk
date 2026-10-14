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

#include <chrono>

#include "ScenarioBuilder.h"
#include "Scenarios.h"

namespace dvfs {

namespace {

// A fixed 30-frame pattern of 6-14 ms loads, the first values the
// former xorshift jitter produced, looped for the 30 s step.
constexpr int kCpuPatternMs[] = {
        6, 7,  13, 10, 14, 14, 7,  8,  6,  6, 9, 6,  8,  12, 9,
        7, 14, 12, 11, 6,  7,  10, 12, 10, 8, 7, 12, 10, 14, 7,
};

} // namespace

// Loops a deterministic 30-frame pattern of 6-14 ms CPU loads that vary frame
// to frame while staying below the 16.7 ms 60 FPS budget.
// Ideal: keep a steady 60 FPS cadence throughout the 30 s step.
// Catches: frame-to-frame variance or 14 ms peaks triggering false downshifts
// to 30 FPS or swap-interval oscillation when every frame fits 60 FPS.
Scenario build_scenario_s4() {
    using namespace std::chrono_literals;
    ScenarioBuilder scenario("S4", "S4: Uniform Random 6-14 ms In-Budget Jitter (30.0s)");
    scenario.gpu(0ms);
    scenario.repeat_for(30s, [](ScenarioBuilder& body) {
        for (int ms : kCpuPatternMs) body.cpu(std::chrono::milliseconds(ms)).frames();
    });
    return scenario.build();
}

} // namespace dvfs
