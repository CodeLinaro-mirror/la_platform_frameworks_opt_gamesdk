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

#pragma once

// A DvfsBenchmark scenario and the ops ScenarioEngine plays. ScenarioBuilder
// builds scenarios.

#include <stdint.h>

#include <string>
#include <vector>

namespace dvfs {

constexpr int kMaxScenarioSteps = 32;

// Swap intervals in nanoseconds, matching SWAPPY_SWAP_60FPS and
// SWAPPY_SWAP_30FPS in swappy_common.h without pulling in Android headers.
constexpr int64_t kSwapInterval60FpsNs = 16666667LL;
constexpr int64_t kSwapInterval30FpsNs = 33333333LL;

enum class ScenarioPipelineMode : int8_t {
    Off = 0,
    On = 1,
    Auto = 2,
};

enum class ScenarioOpKind : uint8_t {
    kStepBegin,                // operand = step index
    kSetCpu,                   // operand = duration in ns
    kSetGpu,                   // operand = duration in ns
    kSetAutoPipelineMode,      // operand = ScenarioPipelineMode, applies with the next frame
    kSetAutoSwapInterval,      // operand = 1 on, 0 off, applies with the next frame
    kSetBufferStuffingFixWait, // operand = frames, applies with the next frame
    kSetSwapIntervalNs,        // operand = swap interval in ns, applies with the next frame
    kResetFramePacing,         // fires with the next frame
    kBoost,                    // operand = duration in ms, fires with the next frame
    kFrames,                   // operand = frame count
    kFramesFor,                // operand = duration in ns
    kRepeat,                   // operand = iterations, partner = its kRepeatEnd
    kRepeatFor,                // operand = duration in ns, partner = its kRepeatEnd
    kRepeatEnd,                // partner = its kRepeat / kRepeatFor
};

struct ScenarioOp {
    ScenarioOpKind kind;
    int64_t operand = 0;
    int32_t partner = -1;
};

class ScenarioBuilder;

// A built scenario: the ops the engine plays and any step names from step().
class Scenario {
public:
    Scenario() = default;
    Scenario(std::string title, std::string description, std::vector<std::string> step_names,
             std::vector<ScenarioOp> ops)
          : mTitle(std::move(title)),
            mDescription(std::move(description)),
            mStepNames(std::move(step_names)),
            mOps(std::move(ops)) {}

    const std::string& title() const {
        return mTitle;
    }
    const std::string& description() const {
        return mDescription;
    }
    const std::vector<std::string>& step_names() const {
        return mStepNames;
    }
    const std::vector<ScenarioOp>& ops() const {
        return mOps;
    }

    // The number of steps in the scenario, including an unnamed step when
    // step_names() is empty.
    int step_count() const;

    // What the scenario needs from the run, read from its ops.
    // Some frame plays a calibrated CPU load. Conservative: a load set inside a
    // repeat body counts, as a later iteration may play it.
    bool uses_cpu() const;
    // Some frame plays a calibrated GPU load, as above.
    bool uses_gpu() const;
    // Boost pulses need the boost thread.
    bool needs_boost() const;

private:
    friend class ScenarioBuilder;

    std::string mTitle;
    std::string mDescription;
    std::vector<std::string> mStepNames;
    std::vector<ScenarioOp> mOps;
};

} // namespace dvfs
