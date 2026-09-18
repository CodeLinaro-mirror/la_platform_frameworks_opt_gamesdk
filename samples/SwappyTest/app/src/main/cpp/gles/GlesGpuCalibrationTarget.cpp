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

#include "GlesGpuCalibrationTarget.h"

#define LOG_TAG "GlesGpuCalib"

#include <cstring>
#include <ctime>
#include <utility>

#include "GlesCircleRenderer.h"
#include "GlesContext.h"
#include "Log.h"

namespace samples::gles {

namespace {

static inline int64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

static constexpr uint64_t kGpuPassTimeoutNs = 1000000000ULL; // 1.0 second timeout per pass

} // namespace

GlesGpuCalibrationTarget::GlesGpuCalibrationTarget(GlesContext& context,
                                                   GlesCircleRenderer& circleRenderer,
                                                   int32_t width, int32_t height, float aspectRatio,
                                                   std::vector<samples::Circle> circles)
      : mContext(context),
        mCircleRenderer(circleRenderer),
        mWidth(width),
        mHeight(height),
        mAspectRatio(aspectRatio),
        mCircles(std::move(circles)) {
    if (mWidth <= 0 || mHeight <= 0) {
        ALOGE("Invalid surface size for offscreen GLES calibration: %dx%d", mWidth, mHeight);
        return;
    }

    // Resolve EGL_KHR_fence_sync extension
    const char* eglExts = eglQueryString(mContext.GetDisplay(), EGL_EXTENSIONS);
    if (eglExts != nullptr && std::strstr(eglExts, "EGL_KHR_fence_sync") != nullptr) {
        mEglCreateSyncKHR =
                reinterpret_cast<PFNEGLCREATESYNCKHRPROC>(eglGetProcAddress("eglCreateSyncKHR"));
        mEglClientWaitSyncKHR = reinterpret_cast<PFNEGLCLIENTWAITSYNCKHRPROC>(
                eglGetProcAddress("eglClientWaitSyncKHR"));
        mEglDestroySyncKHR =
                reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(eglGetProcAddress("eglDestroySyncKHR"));
        mHasFenceSync = (mEglCreateSyncKHR != nullptr && mEglClientWaitSyncKHR != nullptr &&
                         mEglDestroySyncKHR != nullptr);
    }

    // Create offscreen texture
    glGenTextures(1, &mTexture);
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mWidth, mHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Create offscreen framebuffer
    glGenFramebuffers(1, &mFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTexture, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        ALOGE("Offscreen GLES FBO incomplete (status 0x%x)", status);
        glDeleteFramebuffers(1, &mFramebuffer);
        glDeleteTextures(1, &mTexture);
        mFramebuffer = 0;
        mTexture = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GlesGpuCalibrationTarget::~GlesGpuCalibrationTarget() {
    if (mPassInFlight) {
        WaitPass();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (mFramebuffer != 0) {
        glDeleteFramebuffers(1, &mFramebuffer);
        mFramebuffer = 0;
    }
    if (mTexture != 0) {
        glDeleteTextures(1, &mTexture);
        mTexture = 0;
    }
}

const char* GlesGpuCalibrationTarget::WallTimerName() const {
    return mHasFenceSync ? "egl_fence" : "gl_finish";
}

void GlesGpuCalibrationTarget::PreparePass(int32_t /*gpuWorkload*/) {
    if (mFramebuffer != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    }
}

bool GlesGpuCalibrationTarget::SubmitPass(int32_t gpuWorkload) {
    if (mFramebuffer == 0) {
        return false;
    }
    if (mPassInFlight) {
        WaitPass();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
    mCircleRenderer.Draw(mWidth, mHeight, mAspectRatio, mCircles, gpuWorkload, nullptr, nullptr);

    mSubmitTimeNs = get_time_ns();
    if (mHasFenceSync) {
        mActiveSync = mEglCreateSyncKHR(mContext.GetDisplay(), EGL_SYNC_FENCE_KHR, nullptr);
        glFlush();
    } else {
        mActiveSync = EGL_NO_SYNC_KHR;
        glFlush();
    }
    mPassInFlight = true;
    return true;
}

samples::GpuPassTiming GlesGpuCalibrationTarget::WaitPass() {
    if (!mPassInFlight) {
        return {-1};
    }

    bool ok = true;
    if (mHasFenceSync && mActiveSync != EGL_NO_SYNC_KHR) {
        const EGLint status =
                mEglClientWaitSyncKHR(mContext.GetDisplay(), mActiveSync,
                                      EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, kGpuPassTimeoutNs);
        mEglDestroySyncKHR(mContext.GetDisplay(), mActiveSync);
        mActiveSync = EGL_NO_SYNC_KHR;
        if (status != EGL_CONDITION_SATISFIED_KHR) {
            ALOGW("EGL fence wait timeout/failure (status 0x%x)", status);
            ok = false;
        }
    } else {
        glFinish();
    }
    const int64_t wall_ns = get_time_ns() - mSubmitTimeNs;
    mPassInFlight = false;

    if (!ok) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return {-1};
    }
    return {wall_ns};
}

samples::GpuPassTiming GlesGpuCalibrationTarget::RunPass(int32_t gpuWorkload) {
    PreparePass(gpuWorkload);
    if (!SubmitPass(gpuWorkload)) {
        return {-1};
    }
    samples::GpuPassTiming timing = WaitPass();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return timing;
}

} // namespace samples::gles
