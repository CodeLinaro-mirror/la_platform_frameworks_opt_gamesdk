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

#include "GlesContext.h"

#define LOG_TAG "GlesContext"

#include <GLES2/gl2.h>

#include "Log.h"
#include "swappy/swappyGL.h"
#include "swappy/swappyGL_extra.h"

namespace samples::gles {

GlesContext::~GlesContext() {
    Destroy();
}

bool GlesContext::Initialize(ANativeWindow* window) {
    Destroy();
    if (window == nullptr) {
        ALOGE("Cannot initialize GlesContext with a null ANativeWindow");
        return false;
    }

    mDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (mDisplay == EGL_NO_DISPLAY || !eglInitialize(mDisplay, nullptr, nullptr)) {
        ALOGE("Failed to initialize EGL display");
        mDisplay = EGL_NO_DISPLAY;
        return false;
    }

    const EGLint configAttribs[] = {EGL_RENDERABLE_TYPE,
                                    EGL_OPENGL_ES2_BIT,
                                    EGL_RED_SIZE,
                                    8,
                                    EGL_GREEN_SIZE,
                                    8,
                                    EGL_BLUE_SIZE,
                                    8,
                                    EGL_NONE};
    EGLint numConfigs = 0;
    if (!eglChooseConfig(mDisplay, configAttribs, &mConfig, 1, &numConfigs) || numConfigs <= 0) {
        ALOGE("Failed to choose EGL config");
        Destroy();
        return false;
    }

    const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    mContext = eglCreateContext(mDisplay, mConfig, EGL_NO_CONTEXT, contextAttribs);
    mSurface = eglCreateWindowSurface(mDisplay, mConfig, window, nullptr);
    if (mSurface == EGL_NO_SURFACE || mContext == EGL_NO_CONTEXT ||
        !eglMakeCurrent(mDisplay, mSurface, mSurface, mContext)) {
        ALOGE("Failed to create EGL surface or make current");
        Destroy();
        return false;
    }

    glEnable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    return true;
}

void GlesContext::Destroy() {
    if (mDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (mSurface != EGL_NO_SURFACE) {
            eglDestroySurface(mDisplay, mSurface);
            mSurface = EGL_NO_SURFACE;
        }
        if (mContext != EGL_NO_CONTEXT) {
            eglDestroyContext(mDisplay, mContext);
            mContext = EGL_NO_CONTEXT;
        }
        eglTerminate(mDisplay);
        mDisplay = EGL_NO_DISPLAY;
    }
    mConfig = nullptr;
}

void GlesContext::RecordFrameStart(bool useSwappy) const {
    if (useSwappy && IsInitialized()) {
        SwappyGL_recordFrameStart(mDisplay, mSurface);
    }
}

bool GlesContext::SwapBuffers(bool useSwappy) const {
    if (!IsInitialized()) {
        return false;
    }
    if (useSwappy) {
        return SwappyGL_swap(mDisplay, mSurface);
    }
    return eglSwapBuffers(mDisplay, mSurface) == EGL_TRUE;
}

const char* GlesContext::GetVendor() const noexcept {
    const char* s = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    return s != nullptr ? s : "";
}

const char* GlesContext::GetRenderer() const noexcept {
    const char* s = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    return s != nullptr ? s : "";
}

const char* GlesContext::GetVersion() const noexcept {
    const char* s = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    return s != nullptr ? s : "";
}

} // namespace samples::gles
