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

// Steps CPU load up across the 16.7 ms 60 FPS budget (12, 14, 15, 16, 17, 18,
// 20, 27 ms) to test downshift ladder thresholds.
// Ideal: hold 60 FPS while the work fits the budget (12-14 ms) and downshift
// cleanly to 30 FPS once the load exceeds it (18-27 ms), without oscillating
// near the boundary (15-17 ms).
// Catches: downshifting too early on in-budget loads, failing to downshift when
// frames miss 60 FPS deadlines, or thrashing between 30 and 60 FPS near 16 ms.
Scenario build_scenario_s2() {
    using namespace std::chrono_literals;
    ScenarioBuilder scenario("S2",
                             "S2: CPU Ramp-Up 12->27 ms (Downshift Ladder Regression, 24.0s)");
    scenario.gpu(0ms);
    for (int ms : {12, 14, 15, 16, 17, 18, 20, 27}) {
        scenario.step("CPU " + std::to_string(ms) + "ms")
                .cpu(std::chrono::milliseconds(ms))
                .frames_for(3s);
    }
    return scenario.build();
}

} // namespace dvfs
