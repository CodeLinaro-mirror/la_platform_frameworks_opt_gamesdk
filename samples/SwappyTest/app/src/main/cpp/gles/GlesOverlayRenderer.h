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

#include <GLES2/gl2.h>

#include "../OverlayBatcher.h"

namespace samples::gles {

class GlesOverlayRenderer {
public:
    GlesOverlayRenderer() = default;
    ~GlesOverlayRenderer();

    GlesOverlayRenderer(const GlesOverlayRenderer&) = delete;
    GlesOverlayRenderer& operator=(const GlesOverlayRenderer&) = delete;

    /**
     * Compiles the GLES 2.0 overlay shader program, uploads the 128x64 font texture,
     * and pre-allocates dynamic VBO for triangle batches.
     * Returns true on success, false if shader compilation or resource creation fails.
     */
    bool Initialize();

    void Destroy();

    void Draw(const samples::OverlayBatcher& batcher);

private:
    GLuint mProgram = 0;
    GLint mScreenSizeLocation = -1;
    GLint mFontTextureLocation = -1;
    GLint mPositionLocation = -1;
    GLint mUvLocation = -1;
    GLint mColorLocation = -1;

    GLuint mFontTexture = 0;
    GLuint mTriangleVbo = 0;
};

} // namespace samples::gles
