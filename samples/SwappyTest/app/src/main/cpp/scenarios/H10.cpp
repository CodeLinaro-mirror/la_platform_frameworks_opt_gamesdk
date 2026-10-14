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

// Sustains a 22 ms CPU load (too heavy for 60 FPS even at peak clock) while an
// external helper thread fires a 500 ms big-core frequency boost at the start of
// every 6 s step.
// Ideal: stay at 30 FPS throughout all six steps despite the external clock
// boosts.
// Catches: external CPU frequency spikes skewing clock-ratio or work estimates
// and triggering false 60 FPS upshifts on a 22 ms workload.
Scenario build_scenario_h10() {
    using namespace std::chrono_literals;
    ScenarioBuilder
            scenario("H10",
                     "H10: Honeytrap 22 ms Load + 500 ms External Big-Core Boost Every 6s (36.0s)");
    scenario.gpu(0ms);
    for (int part = 1; part <= 6; ++part) {
        scenario.step("CPU 22ms + boost").boost(500ms).cpu(22ms).frames_for(6s);
    }
    return scenario.build();
}

} // namespace dvfs
