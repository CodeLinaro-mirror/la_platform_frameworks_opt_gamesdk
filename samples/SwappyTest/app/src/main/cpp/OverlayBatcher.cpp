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

#include "OverlayBatcher.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace samples {

namespace {

constexpr float kSolidWhiteU = 0.5f / static_cast<float>(OverlayBatcher::kFontAtlasWidth);
constexpr float kSolidWhiteV = 0.5f / static_cast<float>(OverlayBatcher::kFontAtlasHeight);

// Standard 8x8 ASCII bitmap font for printable characters 32 (' ') through 126 ('~').
// Each uint64_t encodes 8 rows (top row in MSB byte, bottom row in LSB byte; MSB of each byte is
// left pixel).
constexpr std::array<uint64_t, 95> kAscii8x8Glyphs = {
        0x0000000000000000ULL, // 32 ' '
        0x183c3c1818001800ULL, // 33 '!'
        0x6c6c480000000000ULL, // 34 '"'
        0x6c6cfe6cfe6c6c00ULL, // 35 '#'
        0x183e603c067c1800ULL, // 36 '$'
        0x00c6cc183066c600ULL, // 37 '%'
        0x386c3876dcce7600ULL, // 38 '&'
        0x1818300000000000ULL, // 39 '''
        0x0c18303030180c00ULL, // 40 '('
        0x30180c0c0c183000ULL, // 41 ')'
        0x00663cff3c660000ULL, // 42 '*'
        0x0018187e18180000ULL, // 43 '+'
        0x0000000000181830ULL, // 44 ','
        0x0000007e00000000ULL, // 45 '-'
        0x0000000000181800ULL, // 46 '.'
        0x060c183060c08000ULL, // 47 '/'
        0x3c666e7666663c00ULL, // 48 '0'
        0x1838181818187e00ULL, // 49 '1'
        0x3c66061c30667e00ULL, // 50 '2'
        0x3c66061c06663c00ULL, // 51 '3'
        0x0c1c3c6c7e0c0c00ULL, // 52 '4'
        0x7e607c0606663c00ULL, // 53 '5'
        0x1c30607c66663c00ULL, // 54 '6'
        0x7e66060c18181800ULL, // 55 '7'
        0x3c66663c66663c00ULL, // 56 '8'
        0x3c66663e060c3800ULL, // 57 '9'
        0x0018180000181800ULL, // 58 ':'
        0x0018180000181830ULL, // 59 ';'
        0x0c18306030180c00ULL, // 60 '<'
        0x00007e00007e0000ULL, // 61 '='
        0x30180c060c183000ULL, // 62 '>'
        0x3c66060c18001800ULL, // 63 '?'
        0x3c666e6e60623c00ULL, // 64 '@'
        0x183c66667e666600ULL, // 65 'A'
        0x7c66667c66667c00ULL, // 66 'B'
        0x3c66606060663c00ULL, // 67 'C'
        0x786c6666666c7800ULL, // 68 'D'
        0x7e60607c60607e00ULL, // 69 'E'
        0x7e60607c60606000ULL, // 70 'F'
        0x3c66606e66663e00ULL, // 71 'G'
        0x6666667e66666600ULL, // 72 'H'
        0x3c18181818183c00ULL, // 73 'I'
        0x1e0c0c0c6c6c3800ULL, // 74 'J'
        0x666c7870786c6600ULL, // 75 'K'
        0x6060606060607e00ULL, // 76 'L'
        0xc6eefed6c6c6c600ULL, // 77 'M'
        0xc6e6f6decec6c600ULL, // 78 'N'
        0x3c66666666663c00ULL, // 79 'O'
        0x7c66667c60606000ULL, // 80 'P'
        0x3c6666666a6c3600ULL, // 81 'Q'
        0x7c66667c6c666600ULL, // 82 'R'
        0x3c66603c06663c00ULL, // 83 'S'
        0x7e18181818181800ULL, // 84 'T'
        0x6666666666663c00ULL, // 85 'U'
        0x66666666663c1800ULL, // 86 'V'
        0xc6c6c6d6feeec600ULL, // 87 'W'
        0xc66c38386cc6c600ULL, // 88 'X'
        0x6666663c18181800ULL, // 89 'Y'
        0xfe0c183060c0fe00ULL, // 90 'Z'
        0x3c30303030303c00ULL, // 91 '['
        0xc06030180c060200ULL, // 92 '\'
        0x3c0c0c0c0c0c3c00ULL, // 93 ']'
        0x10386cc600000000ULL, // 94 '^'
        0x00000000000000ffULL, // 95 '_'
        0x30180c0000000000ULL, // 96 '`'
        0x00003c063e663e00ULL, // 97 'a'
        0x60607c6666667c00ULL, // 98 'b'
        0x00003c6660663c00ULL, // 99 'c'
        0x06063e6666663e00ULL, // 100 'd'
        0x00003c667e603c00ULL, // 101 'e'
        0x1c36307830307800ULL, // 102 'f'
        0x00003e66663e063cULL, // 103 'g'
        0x60607c6666666600ULL, // 104 'h'
        0x1800381818183c00ULL, // 105 'i'
        0x06000e060666663cULL, // 106 'j'
        0x6060666c786c6600ULL, // 107 'k'
        0x3818181818183c00ULL, // 108 'l'
        0x0000ecfed6c6c600ULL, // 109 'm'
        0x00007c6666666600ULL, // 110 'n'
        0x00003c6666663c00ULL, // 111 'o'
        0x00007c66667c6060ULL, // 112 'p'
        0x00003e66663e0606ULL, // 113 'q'
        0x00007c6660606000ULL, // 114 'r'
        0x00003e603c067c00ULL, // 115 's'
        0x30307c3030361c00ULL, // 116 't'
        0x0000666666663e00ULL, // 117 'u'
        0x00006666663c1800ULL, // 118 'v'
        0x0000c6c6d6fe6c00ULL, // 119 'w'
        0x0000c66c386cc600ULL, // 120 'x'
        0x00006666663e067cULL, // 121 'y'
        0x00007e0c18307e00ULL, // 122 'z'
        0x0e18187018180e00ULL, // 123 '{'
        0x1818181818181800ULL, // 124 '|'
        0x7018180e18187000ULL, // 125 '}'
        0x76dc000000000000ULL, // 126 '~'
};

const char* GetClusterLabel(int32_t clusterIndex, int32_t totalClusters) noexcept {
    if (totalClusters <= 1) return "CPU";
    if (totalClusters == 2) return (clusterIndex == 0) ? "LIT" : "BIG";
    if (totalClusters == 3) {
        if (clusterIndex == 0) return "LIT";
        if (clusterIndex == 1) return "MID";
        return "BIG";
    }
    if (clusterIndex == 0) return "LIT";
    if (clusterIndex == 1) return "MID-L";
    if (clusterIndex == 2) return "MID-H";
    return "PRIME";
}

OverlayColor GetClusterFillColor(int32_t clusterIndex, int32_t totalClusters) noexcept {
    if (clusterIndex == totalClusters - 1) {
        return OverlayColor::FromArgb(0xD9FF4081u);
    }
    if (clusterIndex == 0) {
        return OverlayColor::FromArgb(0xD900E676u);
    }
    return OverlayColor::FromArgb(0xD900CCFFu);
}

} // namespace

void OverlayBatcher::BuildFontAtlasPixels(std::span<uint8_t> outPixels) noexcept {
    assert(outPixels.size() == kFontAtlasWidth * kFontAtlasHeight);
    std::fill(outPixels.begin(), outPixels.end(), 0u);

    // Fill glyph cell 0 (top-left 8x8 block) with 0xFF for untextured solid primitives.
    for (uint32_t row = 0; row < kGlyphPixelSize; ++row) {
        for (uint32_t col = 0; col < kGlyphPixelSize; ++col) {
            outPixels[row * kFontAtlasWidth + col] = 0xFFu;
        }
    }

    for (uint32_t ascii = 32; ascii <= 126; ++ascii) {
        const uint64_t bitmap = kAscii8x8Glyphs[ascii - 32];
        const uint32_t atlasCol = ascii % kAtlasColumns;
        const uint32_t atlasRow = ascii / kAtlasColumns;
        const uint32_t baseX = atlasCol * kGlyphPixelSize;
        const uint32_t baseY = atlasRow * kGlyphPixelSize;

        for (uint32_t row = 0; row < kGlyphPixelSize; ++row) {
            const uint8_t rowBits = static_cast<uint8_t>((bitmap >> ((7u - row) * 8u)) & 0xFFu);
            for (uint32_t col = 0; col < kGlyphPixelSize; ++col) {
                const bool bitSet = ((rowBits >> (7u - col)) & 0x1u) != 0;
                outPixels[(baseY + row) * kFontAtlasWidth + (baseX + col)] = bitSet ? 0xFFu : 0x00u;
            }
        }
    }
}

void OverlayBatcher::BeginFrame(float screenWidth, float screenHeight) noexcept {
    mScreenWidth = std::max(1.0f, screenWidth);
    mScreenHeight = std::max(1.0f, screenHeight);
    mTriangleVertexCount = 0;
}

void OverlayBatcher::AddQuad(float left, float top, float right, float bottom,
                             OverlayColor color) noexcept {
    if (mTriangleVertexCount + 6 > kMaxTriangleVertices) {
        return;
    }
    const OverlayVertex v0{left,    top,     kSolidWhiteU, kSolidWhiteV,
                           color.r, color.g, color.b,      color.a};
    const OverlayVertex v1{right,   top,     kSolidWhiteU, kSolidWhiteV,
                           color.r, color.g, color.b,      color.a};
    const OverlayVertex v2{right,   bottom,  kSolidWhiteU, kSolidWhiteV,
                           color.r, color.g, color.b,      color.a};
    const OverlayVertex v3{left,    bottom,  kSolidWhiteU, kSolidWhiteV,
                           color.r, color.g, color.b,      color.a};

    mTriangleVertices[mTriangleVertexCount++] = v0;
    mTriangleVertices[mTriangleVertexCount++] = v1;
    mTriangleVertices[mTriangleVertexCount++] = v2;
    mTriangleVertices[mTriangleVertexCount++] = v0;
    mTriangleVertices[mTriangleVertexCount++] = v2;
    mTriangleVertices[mTriangleVertexCount++] = v3;
}

void OverlayBatcher::AddRectOutline(float left, float top, float right, float bottom,
                                    float thickness, OverlayColor color) noexcept {
    AddQuad(left, top, right, top + thickness, color);
    AddQuad(left, bottom - thickness, right, bottom, color);
    AddQuad(left, top + thickness, left + thickness, bottom - thickness, color);
    AddQuad(right - thickness, top + thickness, right, bottom - thickness, color);
}

void OverlayBatcher::AddThickHorizontalLine(float x0, float x1, float y, float thickness,
                                            OverlayColor color) noexcept {
    const float halfThickness = thickness * 0.5f;
    AddQuad(x0, y - halfThickness, x1, y + halfThickness, color);
}

void OverlayBatcher::DrawGraphHeaderAndLegend(float left, float right, float panelTop,
                                              float densityScale, const char* title,
                                              const char* expText) noexcept {
    const OverlayColor colorTotal = OverlayColor::FromArgb(0xB300C851u);
    const OverlayColor colorGlGpu = OverlayColor::FromArgb(0xFFFF9100u);
    const OverlayColor colorRealCpu = OverlayColor::FromArgb(0xFFFFDD00u);
    const OverlayColor colorExpected = OverlayColor::FromArgb(0xFFFF2A55u);

    const float legendScale = 2.0f * densityScale;
    const float legendY = panelTop + 10.0f * densityScale;
    AddText(left + 14.0f * densityScale, legendY, legendScale, OverlayColor::FromArgb(0xFFFFFFFFu),
            title);

    const float legendRight = right - 14.0f * densityScale;
    const float gap = 14.0f * densityScale;
    const char* cpuText = "# CPU";
    const char* gpuText = "# GPU";
    const char* totText = "# Total";
    const float expW = MeasureTextWidth(expText, legendScale);
    const float cpuW = MeasureTextWidth(cpuText, legendScale);
    const float gpuW = MeasureTextWidth(gpuText, legendScale);
    const float totW = MeasureTextWidth(totText, legendScale);

    const float expX = legendRight - expW;
    const float cpuX = expX - gap - cpuW;
    const float gpuX = cpuX - gap - gpuW;
    const float totX = gpuX - gap - totW;
    AddText(totX, legendY, legendScale, colorTotal, totText);
    AddText(gpuX, legendY, legendScale, colorGlGpu, gpuText);
    AddText(cpuX, legendY, legendScale, colorRealCpu, cpuText);
    AddText(expX, legendY, legendScale, colorExpected, expText);
}

float OverlayBatcher::MeasureTextWidth(const char* text, float scale) noexcept {
    if (text == nullptr) {
        return 0.0f;
    }
    const float pixelScale = std::max(1.0f, std::round(scale));
    const float glyphSize = static_cast<float>(kGlyphPixelSize) * pixelScale;
    return static_cast<float>(std::strlen(text)) * glyphSize;
}

void OverlayBatcher::AddText(float x, float y, float scale, OverlayColor color,
                             const char* text) noexcept {
    if (text == nullptr) {
        return;
    }
    const float pixelScale = std::max(1.0f, std::round(scale));
    const float glyphSize = static_cast<float>(kGlyphPixelSize) * pixelScale;
    const float invAtlasW = 1.0f / static_cast<float>(kFontAtlasWidth);
    const float invAtlasH = 1.0f / static_cast<float>(kFontAtlasHeight);

    const float startX = std::round(x);
    const float y0 = std::round(y);
    const float y1 = y0 + glyphSize;

    size_t charIndex = 0;
    for (const char* p = text; *p != '\0'; ++p, ++charIndex) {
        const uint8_t ch = static_cast<uint8_t>(*p);
        const uint32_t ascii = (ch >= 32u && ch <= 126u) ? ch : static_cast<uint32_t>('?');
        const float x0 = startX + static_cast<float>(charIndex) * glyphSize;
        const float x1 = x0 + glyphSize;
        if (ascii != 32u && mTriangleVertexCount + 6 <= kMaxTriangleVertices) {
            const uint32_t col = ascii % kAtlasColumns;
            const uint32_t row = ascii / kAtlasColumns;
            const float u0 = static_cast<float>(col * kGlyphPixelSize) * invAtlasW;
            const float v0 = static_cast<float>(row * kGlyphPixelSize) * invAtlasH;
            const float u1 = static_cast<float>((col + 1u) * kGlyphPixelSize) * invAtlasW;
            const float v1 = static_cast<float>((row + 1u) * kGlyphPixelSize) * invAtlasH;

            mTriangleVertices[mTriangleVertexCount++] =
                    OverlayVertex{x0, y0, u0, v0, color.r, color.g, color.b, color.a};
            mTriangleVertices[mTriangleVertexCount++] =
                    OverlayVertex{x1, y0, u1, v0, color.r, color.g, color.b, color.a};
            mTriangleVertices[mTriangleVertexCount++] =
                    OverlayVertex{x1, y1, u1, v1, color.r, color.g, color.b, color.a};
            mTriangleVertices[mTriangleVertexCount++] =
                    OverlayVertex{x0, y0, u0, v0, color.r, color.g, color.b, color.a};
            mTriangleVertices[mTriangleVertexCount++] =
                    OverlayVertex{x1, y1, u1, v1, color.r, color.g, color.b, color.a};
            mTriangleVertices[mTriangleVertexCount++] =
                    OverlayVertex{x0, y1, u0, v1, color.r, color.g, color.b, color.a};
        }
    }
}

void OverlayBatcher::BuildTelemetryHud(const TelemetryHudInput& hud) noexcept {
    const float densityScale =
            std::clamp(std::min(mScreenWidth, mScreenHeight) / 1080.0f, 0.75f, 2.5f);
    const float margin = 18.0f * densityScale;
    const float topPanelTop = margin;
    const float topPanelHeight = 148.0f * densityScale;

    BuildLiveTextPanel(hud, densityScale, margin, topPanelTop, topPanelHeight);

    const TelemetryRollingStats rolling = hud.telemetry.computeRollingStats();

    const float freqPanelTop = topPanelTop + topPanelHeight + 12.0f * densityScale;
    const float freqPanelHeight = 196.0f * densityScale;
    BuildFrequencyPanel(rolling, densityScale, margin, freqPanelTop, freqPanelHeight);

    const float graphPanelHeight = std::min(420.0f * densityScale, mScreenHeight * 0.34f);
    const float graphPanelTop = mScreenHeight - margin - graphPanelHeight;
    if (graphPanelTop > freqPanelTop + freqPanelHeight + 12.0f * densityScale) {
        if (hud.histogramMode) {
            const Histogram1sData histogram = hud.telemetry.computeHistogram1s();
            BuildHistogramPanel(hud, rolling.swapDurationMs, histogram, densityScale, margin,
                                graphPanelTop, graphPanelHeight);
        } else {
            BuildFrameTimeGraphPanel(hud, rolling.swapDurationMs, densityScale, margin,
                                     graphPanelTop, graphPanelHeight);
        }
    }
}

void OverlayBatcher::BuildLiveTextPanel(const TelemetryHudInput& hud, float densityScale,
                                        float margin, float panelTop, float panelHeight) noexcept {
    const float left = margin;
    const float right = mScreenWidth - margin;
    const OverlayColor panelBg = OverlayColor::FromArgb(0xCC18181Cu);
    AddQuad(left, panelTop, right, panelTop + panelHeight, panelBg);

    const float textScale = 2.4f * densityScale;
    const float lineStep = 32.0f * densityScale;
    const float textX = left + 16.0f * densityScale;
    float textY = panelTop + 14.0f * densityScale;

    const DvfsUiSnapshot& ui = hud.ui;
    const int32_t phase = ui.test_phase;
    char line[128];

    const char* apiName = get_active_render_api_name();
    if (phase == TEST_PHASE_READY) {
        std::snprintf(line, sizeof(line), "[%s] Exp: Ready [%s]", apiName, ui.test_scenario);
    } else if (phase == TEST_PHASE_COOLDOWN) {
        std::snprintf(line, sizeof(line), "[%s] Exp: Cooldown [%s]", apiName, ui.test_scenario);
    } else {
        std::snprintf(line, sizeof(line), "[%s] Exp: %.1f FPS [%s]", apiName, ui.expected_fps,
                      ui.test_scenario);
    }
    AddText(textX, textY, textScale, OverlayColor::FromArgb(0xFF00E5FFu), line);
    textY += lineStep;

    const bool runningScenarioSteps = (phase == TEST_PHASE_RUNNING);
    const FrameTelemetrySample* latest = hud.telemetry.latestSample();
    const char* pacingStrategyName = hud.pacingStrategyName ? hud.pacingStrategyName : "auto";
    if (runningScenarioSteps && latest != nullptr &&
        (latest->realCpuMs > 0.0f || latest->gpuWorkMs > 0.0f)) {
        std::snprintf(line, sizeof(line), "Real: %.1f FPS (%.1f+%.1fms) | Pacing: %s",
                      ui.measured_real_fps, latest->realCpuMs, latest->gpuWorkMs,
                      pacingStrategyName);
    } else {
        std::snprintf(line, sizeof(line), "Real: %.1f FPS | Pacing: %s", ui.measured_real_fps,
                      pacingStrategyName);
    }
    AddText(textX, textY, textScale, OverlayColor::FromArgb(0xFF00FF88u), line);
    textY += lineStep;

    char calibTag[48];
    if (ui.run_tainted) {
        std::snprintf(calibTag, sizeof(calibTag), " [TAINTED]");
    } else if (runningScenarioSteps && !ui.calib_at_max_freq && ui.device_global_max_freq_mhz > 0) {
        std::snprintf(calibTag, sizeof(calibTag), " [THROTTLED %d/%d]", ui.calib_freq_mhz,
                      ui.device_global_max_freq_mhz);
    } else {
        std::snprintf(calibTag, sizeof(calibTag), " [Calib OK]");
    }

    const int32_t cpuFreqMhz = ui.current_cpu_freq_mhz;
    if (cpuFreqMhz > 0) {
        const int32_t referenceMaxMhz = std::max(ui.current_cpu_max_freq_mhz, cpuFreqMhz);
        const float clockPercent = (referenceMaxMhz > 0)
                ? (static_cast<float>(cpuFreqMhz) * 100.0f / static_cast<float>(referenceMaxMhz))
                : 0.0f;
        std::snprintf(line, sizeof(line), "CPU: Core %d @ %d/%d MHz (%.0f%%)%s",
                      ui.current_cpu_core, cpuFreqMhz, referenceMaxMhz, clockPercent, calibTag);
    } else {
        std::snprintf(line, sizeof(line), "CPU: Core %d (eff %.0f%%)%s", ui.current_cpu_core,
                      std::min(100.0f, ui.cpu_freq_ratio_pct), calibTag);
    }
    AddText(textX, textY, textScale, OverlayColor::FromArgb(0xFFFFDD00u), line);
    textY += lineStep;

    switch (phase) {
        case TEST_PHASE_READY:
            std::snprintf(line, sizeof(line), "Status: Ready");
            break;
        case TEST_PHASE_COOLDOWN:
            std::snprintf(line, sizeof(line), "Cooldown: %d%% headroom (%.1fs)",
                          ui.cooldown_headroom_pct, ui.cooldown_progress_sec);
            break;
        case TEST_PHASE_RUNNING: {
            char stepPrefix[32] = "";
            if (ui.current_step_index >= 0 && ui.scenario_step_count > 0) {
                std::snprintf(stepPrefix, sizeof(stepPrefix),
                              "Step %d/%d: ", ui.current_step_index + 1, ui.scenario_step_count);
            }
            std::snprintf(line, sizeof(line), "%sTarget: %.0f ms (%.1fs left)%s", stepPrefix,
                          ui.target_workload_ms, ui.step_time_remaining_sec,
                          ui.fps_diverged ? " [DIVERGED]" : "");
            break;
        }
        case TEST_PHASE_COMPLETE:
        default: {
            if (ui.run_tainted) {
                std::snprintf(line, sizeof(line), "Complete: TAINTED (%s)", ui.taint_reason);
            } else {
                std::snprintf(line, sizeof(line), "Complete");
            }
            break;
        }
    }
    AddText(textX, textY, textScale, OverlayColor::FromArgb(0xFFE2E8F0u), line);
}

void OverlayBatcher::BuildFrequencyPanel(const TelemetryRollingStats& rolling, float densityScale,
                                         float margin, float panelTop, float panelHeight) noexcept {
    const float left = margin;
    const float right = mScreenWidth - margin;
    const float width = right - left;
    const OverlayColor panelBg = OverlayColor::FromArgb(0xCC18181Cu);
    AddQuad(left, panelTop, right, panelTop + panelHeight, panelBg);

    const float innerMargin = 14.0f * densityScale;
    const float columnGap = 14.0f * densityScale;
    const float usableWidth = width - 2.0f * innerMargin - columnGap;
    const float leftColWidth = usableWidth * 0.53f;
    const float rightColWidth = usableWidth - leftColWidth;
    const float leftColX = left + innerMargin;
    const float rightColX = leftColX + leftColWidth + columnGap;

    const float headerScale = 2.1f * densityScale;
    const float headerY = panelTop + 10.0f * densityScale;
    const OverlayColor headerColor = OverlayColor::FromArgb(0xE6FFFFFFu);
    AddText(leftColX, headerY, headerScale, headerColor, "Avg Freq (% max, 1s)");
    AddText(rightColX, headerY, headerScale, headerColor, "Core Type (% frames, 1s)");

    const int32_t numClusters =
            std::clamp(rolling.clusterCount, 1,
                       static_cast<int32_t>(TelemetryRollingStats::kMaxClusters));
    const int32_t totalRows = numClusters + 1;
    const float topPadding = 36.0f * densityScale;
    const float bottomPadding = 10.0f * densityScale;
    const float plotHeight = panelHeight - topPadding - bottomPadding;
    const float rowGap = 6.0f * densityScale;
    const float rowHeight = (plotHeight - rowGap * static_cast<float>(totalRows - 1)) /
            static_cast<float>(totalRows);
    const float labelScale = 2.1f * densityScale;
    const float glyphHeight = static_cast<float>(kGlyphPixelSize) * labelScale;

    const OverlayColor slotBgColor = OverlayColor::FromArgb(0x26FFFFFFu);
    const OverlayColor cpuFreqFillColor = OverlayColor::FromArgb(0xD900CCFFu);
    const OverlayColor gpuFreqFillColor = OverlayColor::FromArgb(0xD9FF9100u);

    auto drawMeterBar = [&](float barLeft, float barTop, float barWidth, float barBottom,
                            float percent, OverlayColor fillColor, bool isHighlighted,
                            const char* labelText) {
        AddQuad(barLeft, barTop, barLeft + barWidth, barBottom, slotBgColor);
        const float clampedPercent = std::clamp(percent, 0.0f, 100.0f);
        if (clampedPercent > 0.0f) {
            const float fillWidth =
                    std::max(6.0f * densityScale, (clampedPercent / 100.0f) * barWidth);
            AddQuad(barLeft, barTop, barLeft + fillWidth, barBottom, fillColor);
        }
        const OverlayColor borderColor = isHighlighted ? OverlayColor::FromArgb(0xFFFFFFFFu)
                                                       : OverlayColor::FromArgb(0x40FFFFFFu);
        const float borderThickness = isHighlighted ? (2.2f * densityScale) : (1.1f * densityScale);
        AddRectOutline(barLeft, barTop, barLeft + barWidth, barBottom, borderThickness,
                       borderColor);

        const float textY = (barTop + barBottom - glyphHeight) * 0.5f;
        const OverlayColor textColor = isHighlighted ? OverlayColor::FromArgb(0xFFFFFFFFu)
                                                     : OverlayColor::FromArgb(0xD9FFFFFFu);
        AddText(barLeft + 12.0f * densityScale, textY, labelScale, textColor, labelText);
    };

    for (int32_t slot = 0; slot < numClusters; ++slot) {
        const int32_t clusterIndex = (numClusters - 1) - slot;
        const float rowTop =
                panelTop + topPadding + static_cast<float>(slot) * (rowHeight + rowGap);
        const float rowBottom = rowTop + rowHeight;
        const char* clusterName = GetClusterLabel(clusterIndex, numClusters);
        const ClusterRollingStats& stats = rolling.clusterStats[clusterIndex];

        char leftLabel[64];
        std::snprintf(leftLabel, sizeof(leftLabel), "%s Freq: %.0f%%", clusterName,
                      stats.averageFrequencyPercent);
        drawMeterBar(leftColX, rowTop, leftColWidth, rowBottom, stats.averageFrequencyPercent,
                     cpuFreqFillColor, stats.isActiveCluster, leftLabel);

        char rightLabel[64];
        std::snprintf(rightLabel, sizeof(rightLabel), "%s%s: %.0f%%",
                      stats.isActiveCluster ? "> " : "  ", clusterName, stats.residencyPercent);
        drawMeterBar(rightColX, rowTop, rightColWidth, rowBottom, stats.residencyPercent,
                     GetClusterFillColor(clusterIndex, numClusters), stats.isActiveCluster,
                     rightLabel);
    }

    const float gpuRowTop =
            panelTop + topPadding + static_cast<float>(numClusters) * (rowHeight + rowGap);
    const float gpuRowBottom = gpuRowTop + rowHeight;
    char gpuLabel[64];
    if (rolling.averageGpuFrequencyPercent >= 0.0f) {
        std::snprintf(gpuLabel, sizeof(gpuLabel), "GPU Freq: %.0f%%",
                      rolling.averageGpuFrequencyPercent);
    } else {
        std::snprintf(gpuLabel, sizeof(gpuLabel), "GPU Freq: N/A");
    }
    drawMeterBar(leftColX, gpuRowTop, leftColWidth, gpuRowBottom,
                 std::max(0.0f, rolling.averageGpuFrequencyPercent), gpuFreqFillColor,
                 rolling.averageGpuFrequencyPercent > 0.0f, gpuLabel);
}

void OverlayBatcher::BuildFrameTimeGraphPanel(const TelemetryHudInput& hud, float swapDurationMs,
                                              float densityScale, float margin, float panelTop,
                                              float panelHeight) noexcept {
    const float left = margin;
    const float right = mScreenWidth - margin;
    const float width = right - left;
    const float bottom = panelTop + panelHeight;

    const OverlayColor panelBg = OverlayColor::FromArgb(0xCC18181Cu);
    const OverlayColor colorTotal = OverlayColor::FromArgb(0xB300C851u);
    const OverlayColor colorGlGpu = OverlayColor::FromArgb(0xFFFF9100u);
    const OverlayColor colorRealCpu = OverlayColor::FromArgb(0xFFFFDD00u);
    const OverlayColor colorExpected = OverlayColor::FromArgb(0xFFFF2A55u);
    const OverlayColor colorSwapDuration = OverlayColor::FromArgb(0xFF00FF88u);
    const OverlayColor colorNextTarget = OverlayColor::FromArgb(0xFF00E5FFu);
    const OverlayColor colorInactiveLine = OverlayColor::FromArgb(0xB3D0D8E0u);
    const OverlayColor colorBadgeBg = OverlayColor::FromArgb(0xE612141Au);
    const OverlayColor colorBlackBacking = OverlayColor::FromArgb(0xD9000000u);

    AddQuad(left, panelTop, right, bottom, panelBg);

    DrawGraphHeaderAndLegend(left, right, panelTop, densityScale,
                             "Frame Time (150f) [Tap for Hist]", "- Exp");

    const float topPadding = 38.0f * densityScale;
    const float bottomPadding = 20.0f * densityScale;
    const float plotTop = panelTop + topPadding;
    const float plotBottom = bottom - bottomPadding;
    const float plotHeight = plotBottom - plotTop;
    if (plotHeight <= 0.0f) {
        return;
    }

    const float barAreaLeft = left + 14.0f * densityScale;
    const float barAreaWidth = width - 28.0f * densityScale;
    const float barSlotWidth = barAreaWidth / static_cast<float>(TelemetryRingBuffer::kCapacity);
    const float barFillWidth = std::max(2.0f, barSlotWidth - 1.5f * densityScale);
    const float maxValMs = 50.0f;

    bool hasPreviousExpected = false;
    float previousExpectedY = 0.0f;

    const size_t sampleCount = hud.telemetry.sampleCount();
    for (size_t i = 0; i < sampleCount; ++i) {
        const FrameTelemetrySample& sample = hud.telemetry.sampleOldestFirst(i);

        const float totalMs = static_cast<float>(sample.frameDurationNs) / 1000000.0f;
        const float workMs = sample.realCpuMs + sample.gpuWorkMs;
        const float cpuMs = sample.realCpuMs;
        const float expectedMs = sample.expectedWorkMs;

        const float slotLeft = barAreaLeft + static_cast<float>(i) * barSlotWidth;
        const float barLeft = slotLeft + (barSlotWidth - barFillWidth) * 0.5f;
        const float barRight = barLeft + barFillWidth;

        if (totalMs > 0.0f) {
            const float barTop = plotBottom - std::min(1.0f, totalMs / maxValMs) * plotHeight;
            AddQuad(barLeft, std::min(plotBottom - 1.5f, barTop), barRight, plotBottom, colorTotal);
        }
        if (workMs > 0.0f) {
            const float barTop = plotBottom - std::min(1.0f, workMs / maxValMs) * plotHeight;
            AddQuad(barLeft, std::min(plotBottom - 1.5f, barTop), barRight, plotBottom, colorGlGpu);
        }
        if (cpuMs > 0.0f) {
            const float barTop = plotBottom - std::min(1.0f, cpuMs / maxValMs) * plotHeight;
            AddQuad(barLeft, std::min(plotBottom - 1.5f, barTop), barRight, plotBottom,
                    colorRealCpu);
        }

        if (expectedMs > 0.0f) {
            const float expY = plotBottom - std::min(1.0f, expectedMs / maxValMs) * plotHeight;
            const float slotRight = slotLeft + barSlotWidth;
            AddThickHorizontalLine(slotLeft, slotRight, expY, 3.0f * densityScale, colorExpected);
            if (hasPreviousExpected && std::abs(expY - previousExpectedY) > 0.5f) {
                AddQuad(slotLeft - 0.5f, std::min(previousExpectedY, expY), slotLeft + 0.5f,
                        std::max(previousExpectedY, expY), colorExpected);
            }
            previousExpectedY = expY;
            hasPreviousExpected = true;
        } else {
            hasPreviousExpected = false;
        }
    }

    const float guidelineTextScale = 1.9f * densityScale;
    const float guidelinePixelScale = std::max(1.0f, std::round(guidelineTextScale));
    const float visibleInkH = 7.0f * guidelinePixelScale;

    for (int32_t fps : hud.supportedFps) {
        const float lineMs = 1000.0f / static_cast<float>(fps);
        if (lineMs > maxValMs) {
            continue;
        }
        const bool isTarget = std::abs(lineMs - swapDurationMs) < 1.0f;
        const bool isBelowTarget = !isTarget && (lineMs < swapDurationMs);
        const OverlayColor lineColor = isTarget
                ? colorSwapDuration
                : (isBelowTarget ? colorNextTarget : colorInactiveLine);

        const float y = std::round(plotBottom - (lineMs / maxValMs) * plotHeight);
        AddThickHorizontalLine(left, right, y, 3.2f * densityScale, colorBlackBacking);
        AddQuad(left, y - 0.5f, right, y + 0.5f, lineColor);

        const char* tag = isTarget ? " [ACTIVE]" : (isBelowTarget ? " [NEXT]" : "");
        char label[64];
        std::snprintf(label, sizeof(label), "%d FPS (%.1f ms)%s", fps, lineMs, tag);

        const float labelW = MeasureTextWidth(label, guidelineTextScale);
        const float labelX = left + 14.0f * densityScale;
        const float pad = std::round(4.0f * densityScale);
        const float labelY = std::round(std::max(plotTop + pad, y - visibleInkH - pad - 2.0f));
        AddQuad(labelX - pad, labelY - pad, labelX + labelW + pad, labelY + visibleInkH + pad,
                colorBadgeBg);
        AddText(labelX, labelY, guidelineTextScale, lineColor, label);
    }
}

void OverlayBatcher::BuildHistogramPanel(const TelemetryHudInput& hud, float swapDurationMs,
                                         const Histogram1sData& histogram, float densityScale,
                                         float margin, float panelTop, float panelHeight) noexcept {
    const float left = margin;
    const float right = mScreenWidth - margin;
    const float bottom = panelTop + panelHeight;

    const OverlayColor panelBg = OverlayColor::FromArgb(0xCC18181Cu);
    const OverlayColor colorTotal = OverlayColor::FromArgb(0xB300C851u);
    const OverlayColor colorGlGpu = OverlayColor::FromArgb(0xFFFF9100u);
    const OverlayColor colorRealCpu = OverlayColor::FromArgb(0xFFFFDD00u);
    const OverlayColor colorExpected = OverlayColor::FromArgb(0xFFFF2A55u);
    const OverlayColor colorSwapDuration = OverlayColor::FromArgb(0xFF00FF88u);
    const OverlayColor colorNextTarget = OverlayColor::FromArgb(0xFF00E5FFu);
    const OverlayColor colorInactiveLine = OverlayColor::FromArgb(0xB3D0D8E0u);
    const OverlayColor colorBadgeBg = OverlayColor::FromArgb(0xE612141Au);
    const OverlayColor colorBlackBacking = OverlayColor::FromArgb(0xD9000000u);

    AddQuad(left, panelTop, right, bottom, panelBg);

    DrawGraphHeaderAndLegend(left, right, panelTop, densityScale,
                             "Hist 1s (42 bins) [Tap for Time]", "# Exp");

    const float legendY = panelTop + 10.0f * densityScale;

    const float bannerTop = legendY + 22.0f * densityScale;
    const float bannerHeight = 44.0f * densityScale;
    const float bannerBottom = bannerTop + bannerHeight;
    AddQuad(left + 10.0f * densityScale, bannerTop, right - 10.0f * densityScale, bannerBottom,
            colorBadgeBg);

    const float bannerTextScale = 1.9f * densityScale;
    char line1[128];
    std::snprintf(line1, sizeof(line1), "1s (%df): 60FPS %df | 40FPS %df | 30FPS %df | <30 %df",
                  histogram.windowFrames, histogram.frames60, histogram.frames40,
                  histogram.frames30, histogram.framesSlow);
    AddText(left + 18.0f * densityScale, bannerTop + 6.0f * densityScale, bannerTextScale,
            OverlayColor::FromArgb(0xFF00E5FFu), line1);

    char line2[128];
    std::snprintf(line2, sizeof(line2), "Exp: %.1fms | CPU: %.1fms (+%.1fms GPU) | Total: %.1fms",
                  histogram.avgExpMs, histogram.avgCpuMs, histogram.avgGlMs, histogram.avgTotalMs);
    AddText(left + 18.0f * densityScale, bannerTop + 24.0f * densityScale, bannerTextScale,
            OverlayColor::FromArgb(0xFFFFDD00u), line2);

    const float plotLeft = left + 20.0f * densityScale;
    const float plotRight = right - 14.0f * densityScale;
    const float plotTop = bannerBottom + 12.0f * densityScale;
    const float plotBottom = bottom - 22.0f * densityScale;
    const float plotWidth = plotRight - plotLeft;
    const float plotHeight = plotBottom - plotTop;
    if (plotWidth <= 0.0f || plotHeight <= 0.0f) {
        return;
    }

    const bool supports120Fps = std::find(hud.supportedFps.begin(), hud.supportedFps.end(), 120) !=
            hud.supportedFps.end();
    const float scaleMax = supports120Fps ? 120.0f : 60.0f;
    const int tickStep = supports120Fps ? 30 : 15;
    const int maxTick = static_cast<int>(scaleMax);
    for (int tick = tickStep; tick <= maxTick; tick += tickStep) {
        const float gy = plotBottom - (static_cast<float>(tick) / scaleMax) * plotHeight;
        AddQuad(plotLeft, gy - 0.5f, plotRight, gy + 0.5f, OverlayColor::FromArgb(0x33FFFFFFu));
        char tickLabel[16];
        std::snprintf(tickLabel, sizeof(tickLabel), "%df", tick);
        AddText(left + 4.0f * densityScale, gy - 4.0f * densityScale, 1.4f * densityScale,
                OverlayColor::FromArgb(0x80FFFFFFu), tickLabel);
    }

    constexpr size_t numBins = Histogram1sData::kBinCount;
    const float binWidth = plotWidth / static_cast<float>(numBins);
    const float barPad = std::max(0.5f, binWidth * 0.08f);

    for (size_t b = 0; b < numBins; ++b) {
        const int32_t tCount = histogram.totalBins[b];
        const int32_t wCount = histogram.workBins[b];
        const int32_t eCount = histogram.expBins[b];
        if (tCount == 0 && wCount == 0 && eCount == 0) continue;

        const float binLeft = plotLeft + static_cast<float>(b) * binWidth + barPad;
        const float binRight = plotLeft + static_cast<float>(b + 1) * binWidth - barPad;

        const float tHeight =
                (std::min(scaleMax, static_cast<float>(tCount)) / scaleMax) * plotHeight;
        const float wHeight =
                (std::min(scaleMax, static_cast<float>(wCount)) / scaleMax) * plotHeight;
        const float eHeight =
                (std::min(scaleMax, static_cast<float>(eCount)) / scaleMax) * plotHeight;

        if (tCount > 0) {
            AddQuad(binLeft, plotBottom - tHeight, binRight, plotBottom, colorTotal);
        }
        if (wCount > 0) {
            const float cpuFrac = (histogram.workTotalSums[b] > 0.0f)
                    ? (histogram.workCpuSums[b] / histogram.workTotalSums[b])
                    : 1.0f;
            const float workTop = plotBottom - wHeight;
            const float cpuTop = plotBottom - wHeight * std::clamp(cpuFrac, 0.0f, 1.0f);
            AddQuad(binLeft, workTop, binRight, cpuTop, colorGlGpu);
            AddQuad(binLeft, cpuTop, binRight, plotBottom, colorRealCpu);
        }
        if (eCount > 0) {
            const float inset = std::min(2.0f * densityScale, (binRight - binLeft) * 0.25f);
            AddQuad(binLeft + inset, plotBottom - eHeight, binRight - inset, plotBottom,
                    colorExpected);
        }
    }

    for (int32_t fps : hud.supportedFps) {
        const float gateMs = 1000.0f / static_cast<float>(fps);
        if (gateMs >= 42.0f) continue;

        const float gx = plotLeft + (gateMs / 42.0f) * plotWidth;
        const bool isTarget = std::abs(gateMs - swapDurationMs) < 1.2f;
        const bool isBelowTarget = !isTarget && (gateMs < swapDurationMs);
        const OverlayColor lineColor = isTarget
                ? colorSwapDuration
                : (isBelowTarget ? colorNextTarget : colorInactiveLine);

        AddThickHorizontalLine(gx - 1.5f * densityScale, gx + 1.5f * densityScale,
                               (plotTop + plotBottom) * 0.5f, plotHeight, colorBlackBacking);
        AddQuad(gx - 0.5f, plotTop, gx + 0.5f, plotBottom, lineColor);

        char gateLabel[16];
        std::snprintf(gateLabel, sizeof(gateLabel), "%dFPS", fps);
        const float labelW = MeasureTextWidth(gateLabel, 1.6f * densityScale);
        AddQuad(gx - labelW * 0.5f - 2.0f * densityScale, plotBottom + 4.0f * densityScale,
                gx + labelW * 0.5f + 2.0f * densityScale, plotBottom + 18.0f * densityScale,
                colorBadgeBg);
        AddText(gx - labelW * 0.5f, plotBottom + 6.0f * densityScale, 1.6f * densityScale,
                lineColor, gateLabel);
    }
}

} // namespace samples
