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

#include <cstdint>
#include <vector>

#include "../Circle.h"

namespace samples {
class OverlayBatcher;
}

namespace samples::gles {

class GlesOverlayRenderer;

/**
 * Renders the SwappyTest bouncing circle and calibrated fragment-shader GPU workload
 * using OpenGL ES 2.0, symmetrical with samples::vulkan::VulkanCircleRenderer.
 */
class GlesCircleRenderer {
public:
    GlesCircleRenderer() = default;
    ~GlesCircleRenderer() {
        Destroy();
    }

    GlesCircleRenderer(const GlesCircleRenderer&) = delete;
    GlesCircleRenderer& operator=(const GlesCircleRenderer&) = delete;

    [[nodiscard]] bool Initialize();
    void Destroy() noexcept;

    void Draw(int32_t width, int32_t height, float aspectRatio,
              const std::vector<samples::Circle>& circles, int32_t gpuWorkload = 0,
              GlesOverlayRenderer* overlayRenderer = nullptr,
              const samples::OverlayBatcher* overlayBatcher = nullptr);

private:
    bool EnsureProgram();

    GLuint mProgram = 0;
    GLint mColorHandle = -1;
    GLint mGpuWorkloadHandle = -1;
    GLint mRadiusHandle = -1;
    GLint mMvpMatrixHandle = -1;
    GLint mVPositionHandle = -1;
    std::vector<GLfloat> mVertices;
};

} // namespace samples::gles
