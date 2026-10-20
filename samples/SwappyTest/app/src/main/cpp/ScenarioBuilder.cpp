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

#include "ScenarioBuilder.h"

#include <assert.h>

namespace dvfs {

ScenarioBuilder::ScenarioBuilder(std::string title, std::string description) {
    mScenario.mTitle = std::move(title);
    mScenario.mDescription = std::move(description);
}

ScenarioBuilder& ScenarioBuilder::cpu(std::chrono::nanoseconds load) {
    if (load.count() < 0) {
        fail("cpu must not be negative");
    } else if (ok()) {
        emit(ScenarioOpKind::kSetCpu, load.count());
    }
    return *this;
}

ScenarioBuilder& ScenarioBuilder::gpu(std::chrono::nanoseconds load) {
    if (load.count() < 0) {
        fail("gpu must not be negative");
    } else if (ok()) {
        emit(ScenarioOpKind::kSetGpu, load.count());
    }
    return *this;
}

ScenarioBuilder& ScenarioBuilder::set_auto_pipeline_mode(ScenarioPipelineMode mode) {
    change_setting("set_auto_pipeline_mode", ScenarioOpKind::kSetAutoPipelineMode,
                   static_cast<int64_t>(mode));
    return *this;
}

ScenarioBuilder& ScenarioBuilder::set_auto_swap_interval(bool enabled) {
    change_setting("set_auto_swap_interval", ScenarioOpKind::kSetAutoSwapInterval, enabled ? 1 : 0);
    return *this;
}

ScenarioBuilder& ScenarioBuilder::set_buffer_stuffing_fix_wait(int frames) {
    if (frames < 0) {
        fail("set_buffer_stuffing_fix_wait must not be negative");
    } else {
        change_setting("set_buffer_stuffing_fix_wait", ScenarioOpKind::kSetBufferStuffingFixWait,
                       frames);
    }
    return *this;
}

ScenarioBuilder& ScenarioBuilder::set_swap_interval_ns(int64_t swap_ns) {
    if (swap_ns < 1) {
        fail("set_swap_interval_ns must be at least 1 ns");
    } else {
        change_setting("set_swap_interval_ns", ScenarioOpKind::kSetSwapIntervalNs, swap_ns);
    }
    return *this;
}

ScenarioBuilder& ScenarioBuilder::step(std::string name) {
    if (!ok()) return *this;
    if (!mOpenRepeats.empty()) {
        fail("step inside repeat, missing end");
    } else if (mStepState == StepState::kNone || close_step()) {
        open_step(name, StepState::kNamed);
    }
    return *this;
}

ScenarioBuilder& ScenarioBuilder::reset_frame_pacing() {
    if (ok()) emit(ScenarioOpKind::kResetFramePacing);
    return *this;
}

ScenarioBuilder& ScenarioBuilder::boost(std::chrono::milliseconds duration) {
    if (duration.count() <= 0) {
        fail("boost duration must be above 0 ms");
    } else if (ok()) {
        emit(ScenarioOpKind::kBoost, duration.count());
    }
    return *this;
}

ScenarioBuilder& ScenarioBuilder::frames(int64_t count) {
    if (count < 1) {
        fail("frame count must be at least 1");
    } else {
        play_frames(ScenarioOpKind::kFrames, count);
    }
    return *this;
}

ScenarioBuilder& ScenarioBuilder::frames_for(std::chrono::nanoseconds duration) {
    if (duration.count() <= 0) {
        fail("frame duration must be above 0 s");
    } else {
        play_frames(ScenarioOpKind::kFramesFor, duration.count());
    }
    return *this;
}

ScenarioBuilder& ScenarioBuilder::repeat(int64_t count) {
    if (count < 1) {
        fail("repeat count must be at least 1");
    } else {
        open_repeat(ScenarioOpKind::kRepeat, count);
    }
    return *this;
}

ScenarioBuilder& ScenarioBuilder::repeat_for(std::chrono::nanoseconds duration) {
    if (duration.count() <= 0) {
        fail("repeat duration must be above 0 s");
    } else {
        open_repeat(ScenarioOpKind::kRepeatFor, duration.count());
    }
    return *this;
}

ScenarioBuilder& ScenarioBuilder::end_repeat() {
    if (!ok()) return *this;
    if (mOpenRepeats.empty()) {
        fail("unmatched end");
        return *this;
    }
    const OpenRepeat repeat = mOpenRepeats.back();
    mOpenRepeats.pop_back();
    if (!repeat.saw_frame) {
        fail("repeat body has no frames");
        return *this;
    }
    const int end_index = static_cast<int>(mScenario.mOps.size());
    emit(ScenarioOpKind::kRepeatEnd, 0, repeat.op_index);
    mScenario.mOps[repeat.op_index].partner = end_index;
    return *this;
}

bool ScenarioBuilder::finish() {
    if (mFinished || !ok()) return ok();
    mFinished = true;
    if (!mOpenRepeats.empty()) return fail("missing end for repeat");
    if (mStepState != StepState::kNone) return close_step();
    return true;
}

Scenario ScenarioBuilder::build() {
    finish();
    assert(ok() && "invalid built-in scenario");
    return std::move(mScenario);
}

// Names a step() step "<N>. <name>" (or "<N>." when name is empty); unnamed
// steps leave step_names empty unless the scenario also uses step().
bool ScenarioBuilder::open_step(const std::string& name, StepState state) {
    const int step_index = mStepCount;
    if (step_index >= kMaxScenarioSteps) {
        return fail("more than " + std::to_string(kMaxScenarioSteps) + " steps");
    }
    ++mStepCount;
    emit(ScenarioOpKind::kStepBegin, step_index);
    if (state == StepState::kNamed) {
        while (static_cast<int>(mScenario.mStepNames.size()) < step_index) {
            const int index = static_cast<int>(mScenario.mStepNames.size());
            mScenario.mStepNames.push_back(std::to_string(index + 1) + ".");
        }
        const std::string number = std::to_string(step_index + 1) + ".";
        mScenario.mStepNames.push_back(name.empty() ? number : number + " " + name);
    } else if (!mScenario.mStepNames.empty()) {
        mScenario.mStepNames.push_back(std::to_string(step_index + 1) + ".");
    }
    mStepState = state;
    mStepHasFrames = false;
    return true;
}

// A step without frames would never start, so its settings and actions
// would be lost or leak into the next step.
bool ScenarioBuilder::close_step() {
    if (!mStepHasFrames) {
        if (mStepState == StepState::kUnnamed) return fail("step has no frames");
        return fail("step \"" + mScenario.mStepNames.back() + "\" has no frames");
    }
    mStepState = StepState::kNone;
    mStepHasFrames = false;
    return true;
}

bool ScenarioBuilder::open_repeat(ScenarioOpKind kind, int64_t operand) {
    if (!ok()) return false;
    if (mStepState == StepState::kNone && !open_step("", StepState::kUnnamed)) return false;
    mOpenRepeats.push_back({static_cast<int>(mScenario.mOps.size()), /*saw_frame=*/false});
    emit(kind, operand);
    return true;
}

void ScenarioBuilder::play_frames(ScenarioOpKind kind, int64_t operand) {
    if (!ok()) return;
    if (mStepState == StepState::kNone && !open_step("", StepState::kUnnamed)) return;
    emit(kind, operand);
    mStepHasFrames = true;
    for (OpenRepeat& repeat : mOpenRepeats) repeat.saw_frame = true;
}

// Swappy settings apply with the step's first frame, so they must precede it
// and stay outside repeat blocks, whose later iterations run after it; in the
// prelude they apply with the first step's first frame.
void ScenarioBuilder::change_setting(const char* key, ScenarioOpKind kind, int64_t operand) {
    if (!ok()) return;
    if (mStepHasFrames || !mOpenRepeats.empty()) {
        fail(std::string(key) +
             " must come before the first frame of the step and outside repeat blocks");
        return;
    }
    emit(kind, operand);
}

void ScenarioBuilder::emit(ScenarioOpKind kind, int64_t operand, int32_t partner) {
    ScenarioOp op;
    op.kind = kind;
    op.operand = operand;
    op.partner = partner;
    mScenario.mOps.push_back(op);
}

bool ScenarioBuilder::fail(std::string message) {
    if (ok()) mError = std::move(message);
    return false;
}

} // namespace dvfs
