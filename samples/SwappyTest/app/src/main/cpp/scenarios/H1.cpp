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

// Sustains a constant 27 ms CPU load (above the 16.7 ms 60 FPS budget) for 36 s
// when clocks may scale up and make wall time look deceptively close to 16.7 ms.
// Ideal: downshift to 30 FPS and stay locked at 30 FPS for the entire 36 s step.
// Catches: false upshift attempts to 60 FPS under a steady heavy CPU load,
// which immediately miss 60 FPS deadlines and cause 30/60 FPS thrashing.
Scenario build_scenario_h1() {
    using namespace std::chrono_literals;
    ScenarioBuilder scenario("H1", "H1: Honeytrap Constant 27 ms Heavy Load (36.0s)");
    scenario.gpu(0ms).cpu(27ms).frames_for(36s);
    return scenario.build();
}

} // namespace dvfs
