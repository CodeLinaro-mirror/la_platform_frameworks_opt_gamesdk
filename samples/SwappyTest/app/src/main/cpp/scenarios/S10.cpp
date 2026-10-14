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

// Tests mid-run recovery via resetFramePacing(): a 27 ms spike followed by 6 s
// at 12 ms establishes baseline recovery, then a second 27 ms spike is followed
// by 12 ms with an explicit resetFramePacing() call at step start.
// Ideal: downshift to 30 FPS on both 27 ms spikes and recover immediately to
// 60 FPS on the second 12 ms step when resetFramePacing() clears history and
// backoff state.
// Catches: resetFramePacing() failing to clear accumulated downshift backoff,
// smoothed frame times, or lock-in state after a prior spike.
Scenario build_scenario_s10() {
    using namespace std::chrono_literals;
    ScenarioBuilder scenario("S10",
                             "S10: resetFramePacing() Recovery (27ms -> 12ms -> 27ms -> 12ms "
                             "reset, 18.0s)");
    scenario.gpu(0ms);
    scenario.step("heavy CPU 27ms").cpu(27ms).frames_for(3s);
    scenario.step("CPU 12ms").cpu(12ms).frames_for(6s);
    scenario.step("heavy CPU 27ms again").cpu(27ms).frames_for(3s);
    scenario.step("CPU 12ms after resetFramePacing").reset_frame_pacing().cpu(12ms).frames_for(6s);
    return scenario.build();
}

} // namespace dvfs
