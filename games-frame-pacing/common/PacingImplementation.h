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

#include <chrono>
#include <cstdint>
#include <memory>

#include "FrameDurations.h"
#include "SwapIntervalLadder.h"

namespace swappy {

struct TimingChange {
    std::chrono::nanoseconds oldRefreshPeriod{0};
    std::chrono::nanoseconds newRefreshPeriod{0};
    std::chrono::nanoseconds swapDuration{0};
};

struct PacingResult {
    int32_t autoSwapInterval = 1;
    PipelineMode pipelineMode = PipelineMode::On;
    bool configChanged = false;
    std::chrono::nanoseconds preferredRefreshPeriodHint{0};
};

class PacingImplementation {
public:
    virtual ~PacingImplementation() = default;

    // Factory method: returns the active pacing strategy instance.
    static std::unique_ptr<PacingImplementation> create();

    virtual PacingResult onDisplayTimingsChanged(int32_t currentSwapInterval,
                                                 PipelineMode currentPipelineMode,
                                                 const FrameDurations& durations,
                                                 const TimingChange& change) = 0;

    virtual PacingResult updateSwapInterval(int32_t currentSwapInterval,
                                            PipelineMode currentPipelineMode,
                                            FrameDurations& durations,
                                            std::chrono::nanoseconds refreshPeriod,
                                            std::chrono::nanoseconds swapDuration,
                                            std::chrono::nanoseconds autoSwapIntervalThreshold,
                                            bool pipelineModeAutoMode) = 0;
};

} // namespace swappy
