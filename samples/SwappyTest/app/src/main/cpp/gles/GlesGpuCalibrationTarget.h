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

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <cstdint>
#include <vector>

#include "../Circle.h"
#include "../GpuCalibrationTarget.h"

namespace samples::gles {

class GlesContext;
class GlesCircleRenderer;

/**
 * Offscreen GLES 2.0 target for Swappy-independent GPU calibration.
 * Renders into an offscreen FBO (GL_TEXTURE_2D RGBA8 at surface size)
 * using the benchmark circle geometry and fragment shader workload,
 * measuring wall time via EGL_KHR_fence_sync (fallback: glFinish).
 */
class GlesGpuCalibrationTarget : public samples::GpuCalibrationTarget {
public:
    GlesGpuCalibrationTarget(GlesContext& context, GlesCircleRenderer& circleRenderer,
                             int32_t width, int32_t height, float aspectRatio,
                             std::vector<samples::Circle> circles);

    ~GlesGpuCalibrationTarget() override;

    GlesGpuCalibrationTarget(const GlesGpuCalibrationTarget&) = delete;
    GlesGpuCalibrationTarget& operator=(const GlesGpuCalibrationTarget&) = delete;

    [[nodiscard]] bool IsValid() const noexcept {
        return mFramebuffer != 0;
    }

    void PreparePass(int32_t gpuWorkload) override;
    bool SubmitPass(int32_t gpuWorkload) override;
    samples::GpuPassTiming WaitPass() override;
    samples::GpuPassTiming RunPass(int32_t gpuWorkload) override;
    const char* WallTimerName() const override;
    int64_t PixelCount() const override {
        return static_cast<int64_t>(mWidth) * mHeight;
    }

private:
    GlesContext& mContext;
    GlesCircleRenderer& mCircleRenderer;
    int32_t mWidth = 0;
    int32_t mHeight = 0;
    float mAspectRatio = 1.0f;
    std::vector<samples::Circle> mCircles;

    GLuint mTexture = 0;
    GLuint mFramebuffer = 0;

    // EGL fence sync function pointers
    bool mHasFenceSync = false;
    PFNEGLCREATESYNCKHRPROC mEglCreateSyncKHR = nullptr;
    PFNEGLCLIENTWAITSYNCKHRPROC mEglClientWaitSyncKHR = nullptr;
    PFNEGLDESTROYSYNCKHRPROC mEglDestroySyncKHR = nullptr;

    // Pipelined pass state
    EGLSyncKHR mActiveSync = EGL_NO_SYNC_KHR;
    int64_t mSubmitTimeNs = 0;
    bool mPassInFlight = false;
};

} // namespace samples::gles
