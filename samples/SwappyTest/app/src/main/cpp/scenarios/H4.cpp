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

// A fixed 30-frame pattern of 20-30 ms loads, the first values the
// former xorshift jitter produced, looped for the 36 s step.
constexpr int kCpuPatternMs[] = {
        26, 25, 27, 26, 30, 20, 24, 20, 29, 26, 25, 27, 25, 22, 25,
        23, 28, 27, 27, 26, 23, 22, 20, 21, 29, 21, 26, 28, 24, 27,
};

} // namespace

// Loops a deterministic 30-frame pattern of 20-30 ms CPU loads that stay above
// the 16.7 ms 60 FPS budget while occasionally dipping to 20 ms.
// Ideal: downshift to 30 FPS and stay at 30 FPS throughout the 36 s step.
// Catches: short runs of lighter 20-22 ms frames fooling the pacer or EWMA
// filter into false 60 FPS upshifts during a noisy 30 FPS workload.
Scenario build_scenario_h4() {
    using namespace std::chrono_literals;
    ScenarioBuilder scenario("H4", "H4: Honeytrap Uniform Random 20-30 ms (36.0s)");
    scenario.gpu(0ms);
    scenario.repeat_for(36s, [](ScenarioBuilder& body) {
        for (int ms : kCpuPatternMs) body.cpu(std::chrono::milliseconds(ms)).frames();
    });
    return scenario.build();
}

} // namespace dvfs
