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

// Sustains a 25 ms GPU fragment-shader load with only 1 ms of CPU work for 36 s,
// leaving the CPU mostly idle while every frame is GPU-bound above 16.7 ms.
// Ideal: downshift to 30 FPS and stay at 30 FPS throughout the 36 s step.
// Catches: CPU-centric pacers or CPU spin probes that see a 1 ms CPU time and
// repeatedly false-upshift to 60 FPS despite the 25 ms GPU bottleneck.
Scenario build_scenario_h6() {
    using namespace std::chrono_literals;
    ScenarioBuilder scenario("H6",
                             "H6: Honeytrap GPU-Bound 25 ms Fragment Shader + 1 ms CPU (36.0s)");
    scenario.cpu(1ms).gpu(25ms).frames_for(36s);
    return scenario.build();
}

} // namespace dvfs
