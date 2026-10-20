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

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "DvfsBenchmark.h"
#include "TelemetryRingBuffer.h"

namespace samples {

struct TelemetryHudInput {
    const TelemetryRingBuffer& telemetry;
    DvfsUiSnapshot ui{};
    const char* pacingStrategyName = nullptr;
    std::vector<int32_t> supportedFps = {60, 40, 30, 15};
    bool histogramMode = false;
};

struct OverlayVertex {
    float x;
    float y;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
};
static_assert(sizeof(OverlayVertex) == 32, "OverlayVertex must be 32 bytes");

struct OverlayColor {
    float r;
    float g;
    float b;
    float a;

    static constexpr OverlayColor FromArgb(uint32_t argb) noexcept {
        return {
                static_cast<float>((argb >> 16) & 0xFFu) / 255.0f,
                static_cast<float>((argb >> 8) & 0xFFu) / 255.0f,
                static_cast<float>(argb & 0xFFu) / 255.0f,
                static_cast<float>((argb >> 24) & 0xFFu) / 255.0f,
        };
    }
};

class OverlayBatcher {
public:
    static constexpr uint32_t kFontAtlasWidth = 128;
    static constexpr uint32_t kFontAtlasHeight = 64;
    static constexpr uint32_t kGlyphPixelSize = 8;
    static constexpr uint32_t kAtlasColumns = 16;

    static constexpr size_t kMaxTriangleVertices = 12288;

    OverlayBatcher() = default;

    /**
     * Populates a 128x64 single-channel (R8 / LUMINANCE) bitmap font texture.
     * Cell 0 (top-left 8x8 block) is filled with 0xFF for solid untextured quads/lines.
     * Precondition: outPixels.size() == kFontAtlasWidth * kFontAtlasHeight.
     */
    static void BuildFontAtlasPixels(std::span<uint8_t> outPixels) noexcept;

    void BeginFrame(float screenWidth, float screenHeight) noexcept;

    void AddQuad(float left, float top, float right, float bottom, OverlayColor color) noexcept;
    void AddRectOutline(float left, float top, float right, float bottom, float thickness,
                        OverlayColor color) noexcept;
    void AddThickHorizontalLine(float x0, float x1, float y, float thickness,
                                OverlayColor color) noexcept;
    void AddText(float x, float y, float scale, OverlayColor color, const char* text) noexcept;
    [[nodiscard]] static float MeasureTextWidth(const char* text, float scale) noexcept;

    void BuildTelemetryHud(const TelemetryHudInput& hud) noexcept;

    [[nodiscard]] std::span<const OverlayVertex> GetTriangleVertices() const noexcept {
        return std::span<const OverlayVertex>(mTriangleVertices.data(), mTriangleVertexCount);
    }
    [[nodiscard]] float GetScreenWidth() const noexcept {
        return mScreenWidth;
    }
    [[nodiscard]] float GetScreenHeight() const noexcept {
        return mScreenHeight;
    }

private:
    void DrawGraphHeaderAndLegend(float left, float right, float panelTop, float densityScale,
                                  const char* title, const char* expText) noexcept;
    void BuildLiveTextPanel(const TelemetryHudInput& hud, float densityScale, float margin,
                            float panelTop, float panelHeight) noexcept;
    void BuildFrequencyPanel(const TelemetryRollingStats& rolling, float densityScale, float margin,
                             float panelTop, float panelHeight) noexcept;
    void BuildFrameTimeGraphPanel(const TelemetryHudInput& hud, float swapDurationMs,
                                  float densityScale, float margin, float panelTop,
                                  float panelHeight) noexcept;
    void BuildHistogramPanel(const TelemetryHudInput& hud, float swapDurationMs,
                             const Histogram1sData& histogram, float densityScale, float margin,
                             float panelTop, float panelHeight) noexcept;

    std::array<OverlayVertex, kMaxTriangleVertices> mTriangleVertices{};
    size_t mTriangleVertexCount = 0;

    float mScreenWidth = 1.0f;
    float mScreenHeight = 1.0f;
};

} // namespace samples
