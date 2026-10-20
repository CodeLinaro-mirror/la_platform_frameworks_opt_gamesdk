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

// Runs a minimal 1 ms CPU load for 12 s where the CPU governor drops to its
// lowest frequency and little core while frames still finish in ~1-3 ms.
// Ideal: hold a steady 60 FPS throughout the run with no probing overhead.
// Catches: low-clock telemetry or idle-frame heuristics triggering unnecessary
// downshifts or CPU spin probes when the app is already running at 60 FPS.
Scenario build_scenario_h8() {
    using namespace std::chrono_literals;
    ScenarioBuilder scenario("H8", "H8: Honeytrap Idle App at Full Rate (1 ms load, 12.0s)");
    scenario.gpu(0ms).cpu(1ms).frames_for(12s);
    return scenario.build();
}

} // namespace dvfs
