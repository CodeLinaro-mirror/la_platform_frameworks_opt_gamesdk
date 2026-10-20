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

struct TelemetryRollingStats {
    static constexpr size_t kMaxClusters = 4;

    float swapDurationMs = 16.67f;
    int32_t clusterCount = 3;
    std::array<ClusterRollingStats, kMaxClusters> clusterStats{};
    float averageGpuFrequencyPercent = -1.0f;
};

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

class TelemetryRingBuffer {
public:
    static constexpr size_t kCapacity = 150;
    static constexpr size_t kMaxClusters = TelemetryRollingStats::kMaxClusters;
    static constexpr size_t kRollingFrequencyWindowFrames = 60;
    static constexpr size_t kSwapDurationWindowFrames = 20;

    TelemetryRingBuffer() = default;

    void reset() noexcept;

    void recordFrame(uint64_t frameDurationNs, float gpuWorkMs) noexcept;

    [[nodiscard]] size_t sampleCount() const noexcept {
        return mCount;
    }

    /**
     * Returns the sample at chronological `index` in [0, sampleCount()), where 0 is the
     * oldest sample in the ring buffer and sampleCount() - 1 is the most recent.
     */
    [[nodiscard]] const FrameTelemetrySample& sampleOldestFirst(size_t index) const noexcept;

    /**
     * Returns the most recently recorded sample, or nullptr if the buffer is empty.
     */
    [[nodiscard]] const FrameTelemetrySample* latestSample() const noexcept;

    /**
     * Computes rolling swap duration, CPU cluster residency/frequency, and GPU frequency
     * statistics over the recent frame windows. Returns default baseline values if empty.
     */
    [[nodiscard]] TelemetryRollingStats computeRollingStats() const noexcept;

    /**
     * Aggregates a 1-second frame-time and work-duration histogram from the most recent
     * samples. Returns a zero-initialized histogram if empty.
     */
    [[nodiscard]] Histogram1sData computeHistogram1s() const noexcept;

private:
    [[nodiscard]] const FrameTelemetrySample& sampleNewestFirst(size_t age) const noexcept;
    [[nodiscard]] float computeSwapDurationMs() const noexcept;

    std::array<FrameTelemetrySample, kCapacity> mSamples{};
    size_t mHead = 0;
    size_t mCount = 0;
    int32_t mClusterCount = 3;
};

} // namespace samples
