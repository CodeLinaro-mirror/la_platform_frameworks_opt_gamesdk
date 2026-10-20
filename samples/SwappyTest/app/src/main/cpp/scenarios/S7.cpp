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

// Reproduces GPU DVFS 30 FPS lock-in after a heavy fragment-shader spike: a
// 27 ms GPU spike downshifts pacing to 30 FPS and an 18 ms GPU step lets the
// GPU governor clock down, before the GPU load steps down from 14 ms to 4 ms
// with a 1 ms CPU load.
// Ideal: downshift to 30 FPS on the 27 ms GPU spike, stay at 30 FPS at 18 ms,
// and upshift back to 60 FPS once the GPU load drops into the 14->4 ms range.
// Catches: staying locked at 30 FPS when downclocked GPU completion times hide
// that the shader work fits 60 FPS, or CPU-only heuristics ignoring GPU load.
Scenario build_scenario_s7() {
    using namespace std::chrono_literals;
    ScenarioBuilder scenario("S7", "S7: GPU Ramp-Down (27, 18, 14->4 ms GPU, 1 ms CPU, 39.0s)");
    scenario.cpu(1ms);
    scenario.step("GPU 27ms spike").gpu(27ms).frames_for(3s);
    scenario.step("GPU 18ms").gpu(18ms).frames_for(3s);
    for (int ms = 14; ms >= 4; --ms) {
        scenario.step("GPU " + std::to_string(ms) + "ms")
                .gpu(std::chrono::milliseconds(ms))
                .frames_for(3s);
    }
    return scenario.build();
}

} // namespace dvfs
