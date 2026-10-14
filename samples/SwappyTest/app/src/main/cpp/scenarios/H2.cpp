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

// Holds a marginal 18 ms CPU load (just above the 16.7 ms 60 FPS budget) for
// 24 s, then drops to a 14 ms CPU load (just below the budget) for 24 s.
// Ideal: stay at 30 FPS throughout the 18 ms half without false upshifts, then
// upshift to 60 FPS once the load drops to 14 ms.
// Catches: optimistic clock projection or loose thresholds false-upshifting on
// 18 ms frames, or overly conservative margins refusing to upshift on 14 ms.
Scenario build_scenario_h2() {
    using namespace std::chrono_literals;
    ScenarioBuilder scenario("H2", "H2: Honeytrap Marginal 18 ms (24s) then 14 ms Mirror (24s)");
    scenario.gpu(0ms);
    scenario.step("CPU 18ms").cpu(18ms).frames_for(24s);
    scenario.step("CPU 14ms").cpu(14ms).frames_for(24s);
    return scenario.build();
}

} // namespace dvfs
