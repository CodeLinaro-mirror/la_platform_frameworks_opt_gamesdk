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

// Flips the bottleneck from CPU-heavy (24 ms CPU, 0 ms GPU) to GPU-heavy (1 ms
// CPU, 24 ms GPU) and then to GPU-light (1 ms CPU, 10 ms GPU), 10 s each.
// Ideal: stay at 30 FPS during both 24 ms phases (CPU-bound and GPU-bound) and
// upshift to 60 FPS once the GPU load drops to 10 ms.
// Catches: false upshifting to 60 FPS when the CPU load drops from 24 ms to 1 ms
// while the GPU takes over at 24 ms, or failing to upshift when the GPU load
// finally drops to 10 ms.
Scenario build_scenario_h9() {
    using namespace std::chrono_literals;
    ScenarioBuilder scenario("H9",
                             "H9: Bottleneck Flip CPU-Heavy (24ms) -> GPU-Heavy (24ms) -> "
                             "GPU-Light (10ms)");
    scenario.step("heavy CPU 24ms").cpu(24ms).gpu(0ms).frames_for(10s);
    scenario.step("heavy GPU 24ms").cpu(1ms).gpu(24ms).frames_for(10s);
    scenario.step("light GPU 10ms").cpu(1ms).gpu(10ms).frames_for(10s);
    return scenario.build();
}

} // namespace dvfs
