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

// Builds a Scenario one command at a time:
//
//   ScenarioBuilder("S10", "S10: ...")   title and description
//       .gpu(0ms)                        loads persist across steps
//       .step("CPU 27ms").cpu(27ms).frames_for(3s)
//       .step("reset").reset_frame_pacing().cpu(12ms).frames_for(6s)
//       .build();
//
// A step lasts as long as the frames it plays, up to the next step() or the
// end of the scenario. frames() outside a step opens an unnamed step that
// lasts to the next step() or the end of the scenario.
// Swappy settings (set_auto_pipeline_mode, set_auto_swap_interval,
// set_buffer_stuffing_fix_wait, set_swap_interval_ns) apply with the step's
// first frame, so they go before it and outside repeat blocks; in the prelude
// they apply with the first step's first frame.
// reset_frame_pacing() and boost() fire with the next frame. Repeat blocks
// nest and run a number of times or for a duration: a timed block's clock
// starts at its first frame. Steps and repeat bodies play at least one frame;
// counts are >= 1 and durations > 0.
//
// The first invalid command is recorded in error() and the builder ignores
// the commands after it.

#include <stdint.h>

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include "Scenario.h"

namespace dvfs {

class ScenarioBuilder {
public:
    // A scenario without a title and description.
    ScenarioBuilder() = default;
    ScenarioBuilder(std::string title, std::string description);

    // The calibrated loads the following frames play; 0 ns plays none.
    ScenarioBuilder& cpu(std::chrono::nanoseconds load);
    ScenarioBuilder& gpu(std::chrono::nanoseconds load);

    ScenarioBuilder& set_auto_pipeline_mode(ScenarioPipelineMode mode);
    ScenarioBuilder& set_auto_swap_interval(bool enabled);
    ScenarioBuilder& set_buffer_stuffing_fix_wait(int frames);
    ScenarioBuilder& set_swap_interval_ns(int64_t swap_ns);
    ScenarioBuilder& set_swap_interval_ns(std::chrono::nanoseconds swap_interval) {
        return set_swap_interval_ns(swap_interval.count());
    }

    // Opens a step named "<N>. <name>", N its 1-based number in the scenario,
    // closing the previous step if one is open.
    ScenarioBuilder& step(std::string name);

    ScenarioBuilder& reset_frame_pacing();
    ScenarioBuilder& boost(std::chrono::milliseconds duration);

    ScenarioBuilder& frames(int64_t count = 1);
    ScenarioBuilder& frames_for(std::chrono::nanoseconds duration);

    // A repeat block: repeat() or repeat_for(), its body, then end_repeat().
    ScenarioBuilder& repeat(int64_t count);
    ScenarioBuilder& repeat_for(std::chrono::nanoseconds duration);
    ScenarioBuilder& end_repeat();

    // A repeat block whose body body(*this) adds.
    template <typename Body>
    ScenarioBuilder& repeat(int64_t count, Body&& body) {
        repeat(count);
        std::forward<Body>(body)(*this);
        return end_repeat();
    }

    template <typename Body>
    ScenarioBuilder& repeat_for(std::chrono::nanoseconds duration, Body&& body) {
        repeat_for(duration);
        std::forward<Body>(body)(*this);
        return end_repeat();
    }

    // Ends the scenario: closes the open step and reports a repeat block left
    // open or a step without frames. Returns ok().
    bool finish();

    bool ok() const {
        return mError.empty();
    }

    // The first invalid command, empty while every command was valid.
    const std::string& error() const {
        return mError;
    }

    // Finishes the scenario and hands it over. Built-in scenarios are valid by
    // construction, so an error here is a programmer error.
    Scenario build();

private:
    // A step opens with step() or, unnamed, with the first frames() or
    // repeat() outside any step.
    enum class StepState { kNone, kUnnamed, kNamed };

    struct OpenRepeat {
        int op_index;
        // The body played a frame, directly or nested.
        bool saw_frame;
    };

    bool open_step(const std::string& name, StepState state);
    bool close_step();
    bool open_repeat(ScenarioOpKind kind, int64_t operand);
    void play_frames(ScenarioOpKind kind, int64_t operand);
    void change_setting(const char* key, ScenarioOpKind kind, int64_t operand);
    void emit(ScenarioOpKind kind, int64_t operand = 0, int32_t partner = -1);
    bool fail(std::string message);

    Scenario mScenario;
    std::string mError;
    bool mFinished = false;
    int mStepCount = 0;
    StepState mStepState = StepState::kNone;
    bool mStepHasFrames = false;
    std::vector<OpenRepeat> mOpenRepeats;
};

} // namespace dvfs
