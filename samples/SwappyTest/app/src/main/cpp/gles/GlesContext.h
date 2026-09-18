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
#include <android/native_window.h>

namespace samples::gles {

/**
 * Manages core EGL/GLES 2.0 state: EGLDisplay, EGLConfig, EGLContext, and EGLSurface,
 * routing presentation through SwappyGL_swap or eglSwapBuffers.
 */
class GlesContext {
public:
    GlesContext() = default;
    ~GlesContext();

    GlesContext(const GlesContext&) = delete;
    GlesContext& operator=(const GlesContext&) = delete;
    GlesContext(GlesContext&&) = delete;
    GlesContext& operator=(GlesContext&&) = delete;

    [[nodiscard]] bool Initialize(ANativeWindow* window);
    void Destroy();

    void RecordFrameStart(bool useSwappy) const;
    bool SwapBuffers(bool useSwappy) const;

    [[nodiscard]] EGLDisplay GetDisplay() const noexcept {
        return mDisplay;
    }
    [[nodiscard]] EGLSurface GetSurface() const noexcept {
        return mSurface;
    }
    [[nodiscard]] EGLContext GetContext() const noexcept {
        return mContext;
    }
    [[nodiscard]] bool IsInitialized() const noexcept {
        return mDisplay != EGL_NO_DISPLAY && mSurface != EGL_NO_SURFACE &&
                mContext != EGL_NO_CONTEXT;
    }

    [[nodiscard]] const char* GetVendor() const noexcept;
    [[nodiscard]] const char* GetRenderer() const noexcept;
    [[nodiscard]] const char* GetVersion() const noexcept;

private:
    EGLDisplay mDisplay = EGL_NO_DISPLAY;
    EGLConfig mConfig = nullptr;
    EGLContext mContext = EGL_NO_CONTEXT;
    EGLSurface mSurface = EGL_NO_SURFACE;
};

} // namespace samples::gles
