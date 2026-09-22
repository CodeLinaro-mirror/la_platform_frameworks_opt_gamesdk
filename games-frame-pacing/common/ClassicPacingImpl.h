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

#if defined(__ANDROID__)
#ifndef LOG_TAG
#define LOG_TAG "SwappyCommon"
#endif
#include "SwappyLog.h"
#else
#ifndef SWAPPY_LOGV
#define SWAPPY_LOGV(...)
#endif
#endif

#include "PacingImplementation.h"

namespace swappy {

class ClassicPacingImplementation final : public PacingImplementation {
public:
    PacingResult updateSwapInterval(int32_t currentSwapInterval, PipelineMode currentPipelineMode,
                                    FrameDurations& durations,
                                    std::chrono::nanoseconds refreshPeriod,
                                    std::chrono::nanoseconds swapDuration,
                                    std::chrono::nanoseconds autoSwapIntervalThreshold,
                                    bool pipelineModeAutoMode) override {
        using namespace std::chrono_literals;

        const auto input = makeLadderInput(durations, refreshPeriod, swapDuration,
                                           autoSwapIntervalThreshold, pipelineModeAutoMode);
        if (!input) {
            return {currentSwapInterval, currentPipelineMode, false, 0ns};
        }

        const LadderState current = {currentSwapInterval, currentPipelineMode};
        const LadderDecision decision = decideSwapInterval(current, *input);

        SWAPPY_LOGV("mPipelineMode = %d", static_cast<int>(currentPipelineMode));
        SWAPPY_LOGV("Average cpu frame time = %.2f",
                    (input->averageFrameTime.getCpuTime().count()) / 1e6f);
        SWAPPY_LOGV("Average gpu frame time = %.2f",
                    (input->averageFrameTime.getGpuTime().count()) / 1e6f);
        SWAPPY_LOGV("upperBound = %.2f", decision.upperBound.count() / 1e6f);
        SWAPPY_LOGV("lowerBound = %.2f", decision.lowerBound.count() / 1e6f);
        SWAPPY_LOGV("frame missed = %d%%", input->missedFramesPercent);
        SWAPPY_LOGV("pipelineFrameTime = %.2f", decision.preferredRefreshPeriodHint.count() / 1e6f);

        switch (decision.action) {
            case LadderAction::SwapSlower:
                SWAPPY_LOGV("Rendering takes too much time for the given config");
                break;
            case LadderAction::SwapFaster:
                SWAPPY_LOGV("Rendering is much shorter for the given config");
                break;
            case LadderAction::PipelineOff:
                SWAPPY_LOGV("Rendering time fits the current swap interval without pipelining");
                break;
            case LadderAction::None:
                break;
        }

        if (decision.next.autoSwapInterval != currentSwapInterval) {
            SWAPPY_LOGV("Changing Swap interval to %d from %d", decision.next.autoSwapInterval,
                        currentSwapInterval);
        }
        if (decision.next.pipelineMode != currentPipelineMode) {
            SWAPPY_LOGV("Turning %s pipelining",
                        decision.next.pipelineMode == PipelineMode::On ? "on" : "off");
        }

        if (decision.configChanged) {
            durations.clear();
        }

        return PacingResult{
                decision.next.autoSwapInterval,
                decision.next.pipelineMode,
                decision.configChanged,
                decision.preferredRefreshPeriodHint,
        };
    }

    PacingResult onDisplayTimingsChanged(int32_t /*currentSwapInterval*/,
                                         PipelineMode /*currentPipelineMode*/,
                                         const FrameDurations& durations,
                                         const TimingChange& change) override {
        using namespace std::chrono_literals;
        const auto pipelineFrameTime = durations.getAverageFrameTime().getTime(PipelineMode::On);
        const auto swapDuration =
                pipelineFrameTime != 0ns ? pipelineFrameTime : change.swapDuration;
        const auto autoSwapInterval = calculateSwapInterval(swapDuration, change.newRefreshPeriod);
        return PacingResult{autoSwapInterval, PipelineMode::On, false, 0ns};
    }
};

} // namespace swappy
