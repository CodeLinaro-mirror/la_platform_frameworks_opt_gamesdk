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

// Alternates 5 s of 27 ms (30 FPS) and 5 s of 10 ms (60 FPS) CPU load over four
// periods to test recovery from repeated heavy spikes. Four periods span the
// projected ladder's first three upshift attempts (~2, 12, and 30 s after the
// first light half-period starts).
// Ideal: pace at 30 FPS during each 27 ms half-period and return to 60 FPS
// during each 10 ms half-period.
// Catches: exponential upshift backoff from prior spikes keeping the pacer
// stuck at 30 FPS during later 10 ms windows, or staying at 60 FPS and missing
// deadlines during 27 ms windows.
Scenario build_scenario_h3() {
    using namespace std::chrono_literals;
    ScenarioBuilder scenario("H3",
                             "H3: Honeytrap Square Wave 27 ms <-> 10 ms (5s half-period, 40s)");
    scenario.gpu(0ms);
    for (int period = 1; period <= 4; ++period) {
        scenario.step("heavy CPU 27ms").cpu(27ms).frames_for(5s);
        scenario.step("light CPU 10ms").cpu(10ms).frames_for(5s);
    }
    return scenario.build();
}

} // namespace dvfs
