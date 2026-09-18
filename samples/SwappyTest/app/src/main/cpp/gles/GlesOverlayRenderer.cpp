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

#include "GlesOverlayRenderer.h"

#define LOG_TAG "GlesOverlayRenderer"

#include <array>
#include <cstddef>
#include <vector>

#include "Log.h"

namespace samples::gles {

namespace {

constexpr const char* kOverlayVertexShader = R"(
uniform vec2 uScreenSize;
attribute vec2 aPos;
attribute vec2 aUv;
attribute vec4 aColor;
varying vec2 vUv;
varying vec4 vColor;
void main() {
    vUv = aUv;
    vColor = aColor;
    vec2 ndc = vec2(
        (aPos.x / uScreenSize.x) * 2.0 - 1.0,
        1.0 - (aPos.y / uScreenSize.y) * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

constexpr const char* kOverlayFragmentShader = R"(
precision mediump float;
varying vec2 vUv;
varying vec4 vColor;
uniform sampler2D uFontTex;
void main() {
    float mask = texture2D(uFontTex, vUv).r;
    gl_FragColor = vec4(vColor.rgb, vColor.a * mask);
}
)";

GLuint CompileGlShader(GLenum shaderType, const char* source) {
    GLuint shader = glCreateShader(shaderType);
    if (shader == 0) {
        return 0;
    }
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLength);
        if (infoLength > 0) {
            std::vector<char> logBuffer(static_cast<size_t>(infoLength), '\0');
            glGetShaderInfoLog(shader, infoLength, nullptr, logBuffer.data());
            ALOGE("GLES overlay shader compile error: %s", logBuffer.data());
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

GlesOverlayRenderer::~GlesOverlayRenderer() {
    Destroy();
}

bool GlesOverlayRenderer::Initialize() {
    Destroy();

    const GLuint vertexShader = CompileGlShader(GL_VERTEX_SHADER, kOverlayVertexShader);
    const GLuint fragmentShader = CompileGlShader(GL_FRAGMENT_SHADER, kOverlayFragmentShader);
    if (vertexShader == 0 || fragmentShader == 0) {
        if (vertexShader != 0) glDeleteShader(vertexShader);
        if (fragmentShader != 0) glDeleteShader(fragmentShader);
        return false;
    }

    mProgram = glCreateProgram();
    glAttachShader(mProgram, vertexShader);
    glAttachShader(mProgram, fragmentShader);
    glLinkProgram(mProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint linked = GL_FALSE;
    glGetProgramiv(mProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(mProgram);
        mProgram = 0;
        return false;
    }

    mScreenSizeLocation = glGetUniformLocation(mProgram, "uScreenSize");
    mFontTextureLocation = glGetUniformLocation(mProgram, "uFontTex");
    mPositionLocation = glGetAttribLocation(mProgram, "aPos");
    mUvLocation = glGetAttribLocation(mProgram, "aUv");
    mColorLocation = glGetAttribLocation(mProgram, "aColor");

    std::array<uint8_t, OverlayBatcher::kFontAtlasWidth * OverlayBatcher::kFontAtlasHeight>
            atlasPixels{};
    OverlayBatcher::BuildFontAtlasPixels(atlasPixels);

    glGenTextures(1, &mFontTexture);
    glBindTexture(GL_TEXTURE_2D, mFontTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, OverlayBatcher::kFontAtlasWidth,
                 OverlayBatcher::kFontAtlasHeight, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                 atlasPixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenBuffers(1, &mTriangleVbo);

    glBindBuffer(GL_ARRAY_BUFFER, mTriangleVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(OverlayBatcher::kMaxTriangleVertices *
                                         sizeof(OverlayVertex)),
                 nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return true;
}

void GlesOverlayRenderer::Destroy() {
    if (mTriangleVbo != 0) {
        glDeleteBuffers(1, &mTriangleVbo);
        mTriangleVbo = 0;
    }
    if (mFontTexture != 0) {
        glDeleteTextures(1, &mFontTexture);
        mFontTexture = 0;
    }
    if (mProgram != 0) {
        glDeleteProgram(mProgram);
        mProgram = 0;
    }
}

void GlesOverlayRenderer::Draw(const OverlayBatcher& batcher) {
    if (mProgram == 0) {
        return;
    }
    const auto triangleVertices = batcher.GetTriangleVertices();
    if (triangleVertices.empty()) {
        return;
    }

    glUseProgram(mProgram);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUniform2f(mScreenSizeLocation, batcher.GetScreenWidth(), batcher.GetScreenHeight());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mFontTexture);
    glUniform1i(mFontTextureLocation, 0);

    const GLuint posLoc = static_cast<GLuint>(mPositionLocation);
    const GLuint uvLoc = static_cast<GLuint>(mUvLocation);
    const GLuint colorLoc = static_cast<GLuint>(mColorLocation);
    glEnableVertexAttribArray(posLoc);
    glEnableVertexAttribArray(uvLoc);
    glEnableVertexAttribArray(colorLoc);

    auto bindVertexLayout = [&]() {
        glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex),
                              reinterpret_cast<const void*>(offsetof(OverlayVertex, x)));
        glVertexAttribPointer(uvLoc, 2, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex),
                              reinterpret_cast<const void*>(offsetof(OverlayVertex, u)));
        glVertexAttribPointer(colorLoc, 4, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex),
                              reinterpret_cast<const void*>(offsetof(OverlayVertex, r)));
    };

    glBindBuffer(GL_ARRAY_BUFFER, mTriangleVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(triangleVertices.size_bytes()),
                    triangleVertices.data());
    bindVertexLayout();
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(triangleVertices.size()));

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(uvLoc);
    glDisableVertexAttribArray(colorLoc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

} // namespace samples::gles
