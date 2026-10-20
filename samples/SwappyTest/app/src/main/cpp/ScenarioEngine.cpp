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

#include "ScenarioEngine.h"

#include <utility>
#include <vector>

namespace dvfs {

void ScenarioEngine::start(Scenario scenario) {
    *this = ScenarioEngine();
    mScenario = std::move(scenario);
    mFinished = mScenario.ops().empty();
}

ScenarioEngine::Frame ScenarioEngine::next_frame(uint64_t now_ns) {
    if (mFinished) return finish();
    const std::vector<ScenarioOp>& ops = mScenario.ops();
    for (;;) {
        if (end_expired_time_block(now_ns)) continue;
        if (mOpIndex >= static_cast<int>(ops.size())) return finish();
        const ScenarioOp& op = ops[mOpIndex];
        switch (op.kind) {
            case ScenarioOpKind::kStepBegin:
                mStepIndex = static_cast<int>(op.operand);
                ++mOpIndex;
                break;
            case ScenarioOpKind::kSetCpu:
                mCpuMs = static_cast<double>(op.operand) / 1e6;
                ++mOpIndex;
                break;
            case ScenarioOpKind::kSetGpu:
                mGpuMs = static_cast<double>(op.operand) / 1e6;
                ++mOpIndex;
                break;
            case ScenarioOpKind::kSetAutoPipelineMode:
                mAutoPipelineModePending = static_cast<ScenarioPipelineMode>(op.operand);
                ++mOpIndex;
                break;
            case ScenarioOpKind::kSetAutoSwapInterval:
                mAutoSwapIntervalPending = op.operand != 0;
                ++mOpIndex;
                break;
            case ScenarioOpKind::kSetBufferStuffingFixWait:
                mBufferStuffingFixWaitPending = static_cast<int>(op.operand);
                ++mOpIndex;
                break;
            case ScenarioOpKind::kSetSwapIntervalNs:
                mSwapIntervalNsPending = op.operand;
                ++mOpIndex;
                break;
            case ScenarioOpKind::kResetFramePacing:
                mResetFramePacingPending = true;
                ++mOpIndex;
                break;
            case ScenarioOpKind::kBoost:
                mBoostMsPending = static_cast<int>(op.operand);
                ++mOpIndex;
                break;
            case ScenarioOpKind::kFrames:
            case ScenarioOpKind::kFramesFor:
                if (run_frames_op(op, now_ns)) return emit_frame(now_ns);
                break;
            case ScenarioOpKind::kRepeat:
            case ScenarioOpKind::kRepeatFor:
                enter_repeat(op);
                break;
            case ScenarioOpKind::kRepeatEnd:
                finish_repeat_iteration(now_ns);
                break;
        }
    }
}

ScenarioEngine::Frame ScenarioEngine::finish() {
    mFinished = true;
    Frame frame;
    frame.finished = true;
    frame.step_index = mStepIndex;
    return frame;
}

// Ends the outermost expired time block, and everything nested in it.
bool ScenarioEngine::end_expired_time_block(uint64_t now_ns) {
    for (size_t depth = 0; depth < mRepeats.size(); ++depth) {
        const RepeatFrame& repeat = mRepeats[depth];
        if (!repeat.timed || !repeat.clock_started || now_ns < repeat.deadline_ns) continue;
        mOpIndex = mScenario.ops()[repeat.op_index].partner + 1;
        mRepeats.resize(depth);
        mFramesOpStarted = false;
        return true;
    }
    return false;
}

// True when the f op at mOpIndex plays a frame now; false once it is used up.
bool ScenarioEngine::run_frames_op(const ScenarioOp& op, uint64_t now_ns) {
    const bool timed = op.kind == ScenarioOpKind::kFramesFor;
    if (!mFramesOpStarted) {
        mFramesOpStarted = true;
        mFramesLeft = op.operand;
        mFramesDeadlineNs = now_ns + static_cast<uint64_t>(op.operand);
    }
    bool plays = false;
    if (timed) {
        plays = now_ns < mFramesDeadlineNs;
    } else if (mFramesLeft > 0) {
        --mFramesLeft;
        plays = true;
    }
    if (!plays) {
        mFramesOpStarted = false;
        ++mOpIndex;
    }
    return plays;
}

void ScenarioEngine::enter_repeat(const ScenarioOp& op) {
    const bool timed = op.kind == ScenarioOpKind::kRepeatFor;
    if (!timed && op.operand <= 0) {
        mOpIndex = op.partner + 1;
        return;
    }
    RepeatFrame repeat{};
    repeat.op_index = mOpIndex;
    repeat.timed = timed;
    repeat.clock_started = false;
    repeat.deadline_ns = 0;
    repeat.iterations_left = op.operand;
    repeat.frames_at_iteration_start = mFramesEmitted;
    mRepeats.push_back(repeat);
    ++mOpIndex;
}

// At a repeat's end: loop while a count block has iterations left or a time
// block has not expired. The builder rejects a body without frames; should a
// body play none anyway, it never loops again, so a time block cannot spin
// forever.
void ScenarioEngine::finish_repeat_iteration(uint64_t now_ns) {
    if (mRepeats.empty()) {
        ++mOpIndex;
        return;
    }
    RepeatFrame& repeat = mRepeats.back();
    bool again = mFramesEmitted != repeat.frames_at_iteration_start;
    if (again && repeat.timed) {
        again = !repeat.clock_started || now_ns < repeat.deadline_ns;
    } else if (again) {
        --repeat.iterations_left;
        again = repeat.iterations_left > 0;
    }
    if (again) {
        repeat.frames_at_iteration_start = mFramesEmitted;
        mOpIndex = repeat.op_index + 1;
    } else {
        mRepeats.pop_back();
        ++mOpIndex;
    }
}

// Plays one frame with the current targets and the pending Swappy settings
// and actions; the time blocks that wait for their first frame start their
// clock.
ScenarioEngine::Frame ScenarioEngine::emit_frame(uint64_t now_ns) {
    ++mFramesEmitted;
    for (RepeatFrame& repeat : mRepeats) {
        if (!repeat.timed || repeat.clock_started) continue;
        repeat.clock_started = true;
        repeat.deadline_ns =
                now_ns + static_cast<uint64_t>(mScenario.ops()[repeat.op_index].operand);
    }
    Frame frame;
    frame.step_index = mStepIndex;
    frame.cpu_ms = mCpuMs;
    frame.gpu_ms = mGpuMs;
    frame.auto_pipeline_mode = mAutoPipelineModePending;
    frame.auto_swap_interval = mAutoSwapIntervalPending;
    frame.buffer_stuffing_fix_wait = mBufferStuffingFixWaitPending;
    frame.swap_interval_ns = mSwapIntervalNsPending;
    frame.reset_frame_pacing = mResetFramePacingPending;
    frame.boost_ms = mBoostMsPending;
    mAutoPipelineModePending.reset();
    mAutoSwapIntervalPending.reset();
    mBufferStuffingFixWaitPending.reset();
    mSwapIntervalNsPending.reset();
    mResetFramePacingPending = false;
    mBoostMsPending = 0;
    return frame;
}

} // namespace dvfs
