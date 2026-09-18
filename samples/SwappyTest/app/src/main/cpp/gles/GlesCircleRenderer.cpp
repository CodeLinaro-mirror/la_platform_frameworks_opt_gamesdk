/*
 * Copyright 2018 The Android Open Source Project
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

#include "GlesCircleRenderer.h"

#define LOG_TAG "GlesCircleRenderer"

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "../OverlayBatcher.h"
#include "GlesOverlayRenderer.h"
#include "Log.h"

namespace samples::gles {

namespace {

std::vector<GLfloat> initializeVertices(int segments) {
    std::vector<GLfloat> vertices(2 * (segments + 2), 0.0f);
    const float dTheta = static_cast<float>(2 * M_PI / segments);
    for (int i = 0; i <= segments; ++i) {
        vertices[(i + 1) * 2] = std::cos(dTheta * i);
        vertices[(i + 1) * 2 + 1] = std::sin(dTheta * i);
    }
    return vertices;
}

constexpr const char* gVertexShader =
        "uniform float uRadius;\n"
        "uniform mat4 uMVPMatrix;\n"
        "attribute vec4 vPosition;\n"
        "void main() {\n"
        "  gl_Position = uMVPMatrix * (vPosition * vec4(vec3(uRadius), 1.0));\n"
        "}\n";

// The workload loop needs highp int: the fragment stage defaults to mediump int,
// which GLSL ES 1.00 only guarantees for values in (-2^10, 2^10).
std::string buildFragmentShader() {
    return "precision highp float;\n"
           "precision highp int;\n"
           "uniform mediump vec3 uColor;\n"
           "uniform int uGpuWorkload;\n"
           "const int kMaxGpuIterations = " +
            std::to_string(samples::Circle::kMaxGpuIterations) +
            ";\n"
            "void main() {\n"
            "  float acc = gl_FragCoord.x * 0.0001 + gl_FragCoord.y * 0.0001;\n"
            "  for (int i = 0; i < kMaxGpuIterations; ++i) {\n"
            "    if (i >= uGpuWorkload) break;\n"
            "    acc = sin(acc + float(i) * 0.001) * 0.9999 + 0.0001;\n"
            "  }\n"
            "  gl_FragColor = vec4(uColor + vec3(acc * 0.00001), 1.0);\n"
            "}\n";
}

void checkGlError(const char* op) {
    for (GLint error = glGetError(); error; error = glGetError()) {
        ALOGI("after %s() glError (0x%x)\n", op, error);
    }
}

GLuint loadShader(GLenum shaderType, const char* pSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader == 0) {
        return shader;
    }

    glShaderSource(shader, 1, &pSource, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLength);
        if (infoLength > 0) {
            std::vector<char> info(infoLength, '\0');
            glGetShaderInfoLog(shader, infoLength, nullptr, info.data());
            ALOGE("Could not compile shader %d:\n%s\n", shaderType, info.data());
            glDeleteShader(shader);
            shader = 0;
        }
    }

    return shader;
}

GLuint createProgram(const char* pVertexSource, const char* pFragmentSource) {
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
    if (!vertexShader) {
        return 0;
    }

    GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
    if (!pixelShader) {
        glDeleteShader(vertexShader);
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program == 0) {
        glDeleteShader(vertexShader);
        glDeleteShader(pixelShader);
        return program;
    }

    glAttachShader(program, vertexShader);
    checkGlError("glAttachShader");
    glAttachShader(program, pixelShader);
    checkGlError("glAttachShader");
    glLinkProgram(program);

    // Flag shaders for deletion once attached to avoid leaks on recreation
    glDeleteShader(vertexShader);
    glDeleteShader(pixelShader);

    GLint linkStatus = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (!linkStatus) {
        GLint infoLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLength);
        if (infoLength > 0) {
            std::vector<char> info(infoLength, '\0');
            glGetProgramInfoLog(program, infoLength, nullptr, info.data());
            ALOGE("Could not link program:\n%s\n", info.data());
        }
        glDeleteProgram(program);
        program = 0;
    }
    return program;
}

std::array<GLfloat, 16> getMvpMatrix(GLfloat aspectRatio, GLfloat x, GLfloat y) {
    std::array<GLfloat, 16> mvpMatrix = {0.0f};
    mvpMatrix[0] = 1.0f / aspectRatio;
    mvpMatrix[5] = 1.0f;
    mvpMatrix[10] = 1.0f;
    mvpMatrix[12] = x;
    mvpMatrix[13] = y;
    mvpMatrix[15] = 1.0f;
    return mvpMatrix;
}

} // namespace

bool GlesCircleRenderer::Initialize() {
    Destroy();
    mVertices = initializeVertices(samples::Circle::MIN_SEGMENTS);
    return EnsureProgram();
}

bool GlesCircleRenderer::EnsureProgram() {
    if (mProgram != 0 && glIsProgram(mProgram)) {
        return true;
    }
    mProgram = createProgram(gVertexShader, buildFragmentShader().c_str());
    if (mProgram == 0) {
        ALOGE("Failed to create circle GLES program");
        checkGlError("createProgram");
        return false;
    }

    mColorHandle = glGetUniformLocation(mProgram, "uColor");
    checkGlError("glGetUniformLocation(uColor)");
    mGpuWorkloadHandle = glGetUniformLocation(mProgram, "uGpuWorkload");
    checkGlError("glGetUniformLocation(uGpuWorkload)");
    mRadiusHandle = glGetUniformLocation(mProgram, "uRadius");
    checkGlError("glGetUniformLocation(uRadius)");
    mMvpMatrixHandle = glGetUniformLocation(mProgram, "uMVPMatrix");
    checkGlError("glGetUniformLocation(uMVPMatrix)");
    mVPositionHandle = glGetAttribLocation(mProgram, "vPosition");
    checkGlError("glGetAttribLocation(vPosition)");
    return true;
}

void GlesCircleRenderer::Destroy() noexcept {
    if (mProgram != 0) {
        glDeleteProgram(mProgram);
        mProgram = 0;
    }
    mColorHandle = -1;
    mGpuWorkloadHandle = -1;
    mRadiusHandle = -1;
    mMvpMatrixHandle = -1;
    mVPositionHandle = -1;
}

void GlesCircleRenderer::Draw(int32_t width, int32_t height, float aspectRatio,
                              const std::vector<samples::Circle>& circles, int32_t gpuWorkload,
                              GlesOverlayRenderer* overlayRenderer,
                              const samples::OverlayBatcher* overlayBatcher) {
    glViewport(0, 0, width, height);
    glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (mVertices.empty()) {
        mVertices = initializeVertices(samples::Circle::MIN_SEGMENTS);
    }

    if (EnsureProgram()) {
        // Keep geometry fixed at MIN_SEGMENTS so changing `gpuWorkload` never triggers
        // CPU-side sin/cos vertex regeneration or dynamic VBO uploads; GPU load is
        // driven purely by the fragment shader's `uGpuWorkload` loop.
        const int segments = samples::Circle::MIN_SEGMENTS;

        glUseProgram(mProgram);
        checkGlError("glUseProgram");

        glUniform1i(mGpuWorkloadHandle, gpuWorkload > 0 ? gpuWorkload : 0);
        checkGlError("glUniform1i(uGpuWorkload)");

        glVertexAttribPointer(static_cast<GLuint>(mVPositionHandle), 2, GL_FLOAT, GL_FALSE, 0,
                              mVertices.data());
        checkGlError("glVertexAttribPointer");

        glEnableVertexAttribArray(static_cast<GLuint>(mVPositionHandle));
        checkGlError("glEnableVertexAttribArray");

        for (const samples::Circle& circle : circles) {
            glUniform3fv(mColorHandle, 1, circle.color.values.data());
            checkGlError("glUniform3fv(color)");

            glUniform1f(mRadiusHandle, circle.radius);
            checkGlError("glUniform1f(radius)");

            auto mvpMatrix = getMvpMatrix(aspectRatio, circle.x, circle.y);
            glUniformMatrix4fv(mMvpMatrixHandle, 1, GL_FALSE, mvpMatrix.data());
            checkGlError("glUniformMatrix4fv(mvpMatrix)");

            glDrawArrays(GL_TRIANGLE_FAN, 0, segments + 2);
            checkGlError("glDrawArrays");
        }
    }

    if (overlayRenderer != nullptr && overlayBatcher != nullptr) {
        overlayRenderer->Draw(*overlayBatcher);
    }
}

} // namespace samples::gles
