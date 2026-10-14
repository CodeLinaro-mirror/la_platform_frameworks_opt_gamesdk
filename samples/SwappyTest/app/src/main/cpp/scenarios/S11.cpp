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

// Sustains a constant 27 ms CPU load for 10 minutes (600 s) as a long-duration
// soak test for 30 FPS pacing stability.
// Ideal: downshift to 30 FPS and hold a steady 30 FPS cadence for the entire
// 600 s run.
// Catches: slow state drift, counter wraparound, or periodic false upshift
// probes that cause jank over extended 30 FPS gameplay.
Scenario build_scenario_s11() {
    using namespace std::chrono_literals;
    ScenarioBuilder scenario("S11", "S11: 10-Minute Heavy Load Soak (27 ms x 600s)");
    scenario.gpu(0ms).cpu(27ms).frames_for(600s);
    return scenario.build();
}

} // namespace dvfs
