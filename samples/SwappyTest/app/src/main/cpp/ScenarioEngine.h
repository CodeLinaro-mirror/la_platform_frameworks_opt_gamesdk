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

// Plays a Scenario one frame at a time. Pure: time is passed in, the caller
// applies the frame targets, Swappy settings and actions to the run state.

#include <stdint.h>

#include <optional>
#include <vector>

#include "Scenario.h"

namespace dvfs {

// Plays a scenario one frame at a time. Keeps the repeat stack and the time
// deadlines. A time block's clock starts at its first frame and is checked
// before every op, so an expired block ends at once, even mid-iteration,
// together with everything nested in it.
class ScenarioEngine {
public:
    // What the engine returns for one frame.
    struct Frame {
        bool finished = false; // the scenario is over, nothing to play
        int step_index = -1;
        double cpu_ms = 0.0;
        double gpu_ms = 0.0;
        // The Swappy settings that change before this frame, unset when unchanged.
        std::optional<ScenarioPipelineMode> auto_pipeline_mode;
        std::optional<bool> auto_swap_interval;
        std::optional<int> buffer_stuffing_fix_wait;
        std::optional<int64_t> swap_interval_ns;
        bool reset_frame_pacing = false; // a reset_frame_pacing action fires before this frame
        int boost_ms = 0;                // a boost pulse in ms fires before this frame when > 0
    };

    void start(Scenario scenario);
    const Scenario& scenario() const {
        return mScenario;
    }
    Frame next_frame(uint64_t now_ns);
    bool finished() const {
        return mFinished;
    }

private:
    struct RepeatFrame {
        int op_index;
        bool timed;
        bool clock_started;
        uint64_t deadline_ns;
        int64_t iterations_left;
        uint64_t frames_at_iteration_start;
    };

    bool end_expired_time_block(uint64_t now_ns);
    bool run_frames_op(const ScenarioOp& op, uint64_t now_ns);
    void enter_repeat(const ScenarioOp& op);
    void finish_repeat_iteration(uint64_t now_ns);
    Frame emit_frame(uint64_t now_ns);
    Frame finish();

    Scenario mScenario;
    std::vector<RepeatFrame> mRepeats;
    int mOpIndex = 0;
    int mStepIndex = -1;
    double mCpuMs = 0.0;
    double mGpuMs = 0.0;
    std::optional<ScenarioPipelineMode> mAutoPipelineModePending;
    std::optional<bool> mAutoSwapIntervalPending;
    std::optional<int> mBufferStuffingFixWaitPending;
    std::optional<int64_t> mSwapIntervalNsPending;
    bool mResetFramePacingPending = false;
    int mBoostMsPending = 0;
    bool mFramesOpStarted = false;
    int64_t mFramesLeft = 0;
    uint64_t mFramesDeadlineNs = 0;
    uint64_t mFramesEmitted = 0;
    bool mFinished = false;
};

} // namespace dvfs
