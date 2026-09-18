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

#include "DvfsBenchmark.h"

namespace samples {

struct FrameTelemetrySample {
    uint64_t frameDurationNs = 0;
    float expectedWorkMs = 0.0f;
    float realCpuMs = 0.0f;
    float gpuWorkMs = 0.0f;
    float cpuFrequencyPercent = 0.0f;
    float gpuFrequencyPercent = -1.0f;
    int32_t cpuClusterIndex = -1;
};

struct ClusterRollingStats {
    float averageFrequencyPercent = 0.0f;
    float residencyPercent = 0.0f;
    bool isActiveCluster = false;
};

struct TelemetrySnapshot {
    static constexpr size_t kBufferCapacity = 150;
    static constexpr size_t kMaxClusters = 4;
    static constexpr size_t kNumFpsGuidelines = 5;
    static constexpr std::array<int32_t, kNumFpsGuidelines> kFpsGuidelines = {120, 60, 40, 30, 15};

    std::array<FrameTelemetrySample, kBufferCapacity> samples{};
    size_t sampleCount = 0;
    size_t oldestSampleIndex = 0;

    float activeCadenceMs = 16.67f;
    std::array<bool, kNumFpsGuidelines> supportedFps = {false, true, true, true, true};

    int32_t clusterCount = 3;
    std::array<ClusterRollingStats, kMaxClusters> clusterStats{};
    float averageGpuFrequencyPercent = -1.0f;

    std::array<char, 128> lineExpectedFps{};
    std::array<char, 128> lineRealFps{};
    std::array<char, 128> lineCpuFrequency{};
    std::array<char, 128> lineTestStatus{};

    struct Histogram1sData {
        static constexpr size_t kBinCount = 42;
        std::array<int32_t, kBinCount> totalBins{};
        std::array<int32_t, kBinCount> workBins{};
        std::array<int32_t, kBinCount> expBins{};
        std::array<float, kBinCount> workCpuSums{};
        std::array<float, kBinCount> workTotalSums{};
        int32_t windowFrames = 0;
        int32_t frames60 = 0;
        int32_t frames40 = 0;
        int32_t frames30 = 0;
        int32_t framesSlow = 0;
        float avgExpMs = 0.0f;
        float avgCpuMs = 0.0f;
        float avgGlMs = 0.0f;
        float avgTotalMs = 0.0f;
    };

    bool histogramMode = false;
    Histogram1sData histogram{};
};

class TelemetryRingBuffer {
public:
    static constexpr size_t kCapacity = TelemetrySnapshot::kBufferCapacity;
    static constexpr size_t kRollingFrequencyWindowFrames = 60;
    static constexpr size_t kActiveCadenceWindowFrames = 20;

    TelemetryRingBuffer() = default;

    void Reset() noexcept;

    void RecordFrame(
            uint64_t frameDurationNs, float gpuWorkMs, const char* pacingStrategyName,
            const std::array<bool, TelemetrySnapshot::kNumFpsGuidelines>& supportedFps) noexcept;

    /**
     * Copies the latest ring buffer state and rolling statistics into outSnapshot.
     * Precondition: outSnapshot != nullptr.
     */
    void CaptureSnapshot(TelemetrySnapshot* outSnapshot) const noexcept;

private:
    void UpdateRollingStatsAndText(const DvfsUiSnapshot& ui,
                                   const char* pacingStrategyName) noexcept;

    std::array<FrameTelemetrySample, kCapacity> mSamples{};
    size_t mHead = 0;
    size_t mCount = 0;
    int32_t mClusterCount = 3;
    std::array<bool, TelemetrySnapshot::kNumFpsGuidelines> mSupportedFps = {false, true, true, true,
                                                                            true};

    float mActiveCadenceMs = 16.67f;
    std::array<ClusterRollingStats, TelemetrySnapshot::kMaxClusters> mClusterStats{};
    float mAverageGpuFrequencyPercent = -1.0f;

    std::array<char, 128> mLineExpectedFps{};
    std::array<char, 128> mLineRealFps{};
    std::array<char, 128> mLineCpuFrequency{};
    std::array<char, 128> mLineTestStatus{};
};

} // namespace samples
