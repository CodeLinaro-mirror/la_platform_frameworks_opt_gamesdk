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

#include <cstdint>

namespace samples {

struct GpuPassTiming {
    int64_t wall_ns = 0; // CPU-observed submit-to-completion (fence) time.
};

class GpuCalibrationTarget {
public:
    virtual ~GpuCalibrationTarget() = default;

    // Pre-records or prepares the next offscreen pass on the CPU so it can be
    // submitted with near-zero turnaround as soon as the active GPU pass finishes.
    virtual void PreparePass(int32_t gpuWorkload) = 0;

    // Stamps submission time and immediately submits the prepared pass to the GPU.
    virtual bool SubmitPass(int32_t gpuWorkload) = 0;

    // Waits for the currently active submitted pass to complete on the GPU and
    // returns its submit-to-completion wall time. Never blocks if no pass is active.
    virtual GpuPassTiming WaitPass() = 0;

    // Convenience helper: runs a single synchronous pass (Prepare + Submit + Wait).
    virtual GpuPassTiming RunPass(int32_t gpuWorkload) = 0;

    virtual const char* WallTimerName() const = 0; // "vk_fence", "egl_fence", or "gl_finish"
    virtual int64_t PixelCount() const = 0;        // width * height of offscreen target
};

} // namespace samples
