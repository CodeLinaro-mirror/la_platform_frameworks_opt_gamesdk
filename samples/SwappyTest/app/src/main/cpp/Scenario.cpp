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

#include "Scenario.h"

#include <algorithm>

namespace dvfs {

namespace {

bool is_frames_op(const ScenarioOp& op) {
    return op.kind == ScenarioOpKind::kFrames || op.kind == ScenarioOpKind::kFramesFor;
}

// Whether some frame plays a load above 0 that set_load_kind ops set. A load
// set inside a repeat body counts even after the body's frames: a later
// iteration plays it.
bool plays_load(const std::vector<ScenarioOp>& ops, ScenarioOpKind set_load_kind) {
    int64_t load_ns = 0;
    int repeat_depth = 0;
    for (const ScenarioOp& op : ops) {
        if (op.kind == set_load_kind) {
            load_ns = op.operand;
            if (load_ns > 0 && repeat_depth > 0) return true;
        } else if (op.kind == ScenarioOpKind::kRepeat || op.kind == ScenarioOpKind::kRepeatFor) {
            ++repeat_depth;
        } else if (op.kind == ScenarioOpKind::kRepeatEnd) {
            --repeat_depth;
        } else if (is_frames_op(op) && load_ns > 0) {
            return true;
        }
    }
    return false;
}

} // namespace

int Scenario::step_count() const {
    return static_cast<int>(std::count_if(mOps.begin(), mOps.end(), [](const ScenarioOp& op) {
        return op.kind == ScenarioOpKind::kStepBegin;
    }));
}

bool Scenario::uses_cpu() const {
    return plays_load(mOps, ScenarioOpKind::kSetCpu);
}

bool Scenario::uses_gpu() const {
    return plays_load(mOps, ScenarioOpKind::kSetGpu);
}

bool Scenario::needs_boost() const {
    return std::any_of(mOps.begin(), mOps.end(),
                       [](const ScenarioOp& op) { return op.kind == ScenarioOpKind::kBoost; });
}

} // namespace dvfs
