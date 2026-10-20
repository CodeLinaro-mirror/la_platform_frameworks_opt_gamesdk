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

// Reproduces CPU DVFS 30 FPS lock-in after a brief heavy spike: a 27 ms spike
// downshifts pacing to 30 FPS and an 18 ms step gives the CPU governor time to
// clock down, before the load steps down from 14 ms to 4 ms (well below the
// 16.7 ms 60 FPS budget at peak clock).
// Ideal: downshift to 30 FPS on the 27 ms spike, stay at 30 FPS at 18 ms, and
// upshift back to 60 FPS once the load drops into the 14->4 ms range.
// Catches: staying locked at 30 FPS through 14->4 ms because downclocked wall
// times still look too slow for 60 FPS, or upshifting prematurely at 18 ms.
Scenario build_scenario_s1() {
    using namespace std::chrono_literals;
    ScenarioBuilder scenario("S1", "S1: Deterministic 39.0s CPU Ramp-Down (27, 18, 14->4 ms)");
    scenario.gpu(0ms);
    scenario.step("CPU 27ms spike").cpu(27ms).frames_for(3s);
    scenario.step("CPU 18ms").cpu(18ms).frames_for(3s);
    for (int ms = 14; ms >= 4; --ms) {
        scenario.step("CPU " + std::to_string(ms) + "ms")
                .cpu(std::chrono::milliseconds(ms))
                .frames_for(3s);
    }
    return scenario.build();
}

} // namespace dvfs
