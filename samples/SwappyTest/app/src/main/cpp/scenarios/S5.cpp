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

// Runs a light 8 ms CPU load while the app explicitly requests a 30 FPS swap
// interval (33.3 ms).
// Ideal: honor the app's 30 FPS cap throughout the run even though 8 ms work
// could easily run at 60 FPS.
// Catches: auto-swap-interval or upshift logic overriding the app's explicit
// 30 FPS floor and pacing at 60 FPS.
Scenario build_scenario_s5() {
    using namespace std::chrono_literals;
    ScenarioBuilder scenario("S5", "S5: Explicit 30 FPS App Request (8 ms load, 10.0s)");
    scenario.set_swap_interval_ns(kSwapInterval30FpsNs).gpu(0ms).cpu(8ms).frames_for(10s);
    return scenario.build();
}

} // namespace dvfs
