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

#include "TelemetryRingBuffer.h"

#include <algorithm>
#include <cassert>

#include "DvfsBenchmark.h"

namespace samples {

void TelemetryRingBuffer::reset() noexcept {
    mHead = 0;
    mCount = 0;
}

void TelemetryRingBuffer::recordFrame(uint64_t frameDurationNs, float gpuWorkMs) noexcept {
    DvfsUiSnapshot ui;
    get_ui_snapshot(&ui);

    FrameTelemetrySample& sample = mSamples[mHead];
    sample.frameDurationNs = frameDurationNs;
    sample.expectedWorkMs = std::max(0.0f, ui.last_frame_expected_ms);
    sample.realCpuMs = std::max(0.0f, ui.last_frame_real_cpu_ms);
    sample.gpuWorkMs = std::max(0.0f, gpuWorkMs);
    sample.cpuFrequencyPercent = std::clamp(ui.cpu_freq_pct, 0.0f, 100.0f);

    const float rawGpuPercent = ui.gpu_freq_pct;
    sample.gpuFrequencyPercent =
            (rawGpuPercent >= 0.0f) ? std::clamp(rawGpuPercent, 0.0f, 100.0f) : -1.0f;
    sample.cpuClusterIndex = ui.current_cpu_cluster;

    const int32_t detectedClusters = ui.cpu_cluster_count;
    if (detectedClusters >= 1 && detectedClusters <= static_cast<int32_t>(kMaxClusters)) {
        mClusterCount = detectedClusters;
    }

    mHead = (mHead + 1) % kCapacity;
    if (mCount < kCapacity) {
        ++mCount;
    }
}

const FrameTelemetrySample& TelemetryRingBuffer::sampleOldestFirst(size_t index) const noexcept {
    assert(index < mCount);
    const size_t oldestIndex = (mCount < kCapacity) ? 0 : mHead;
    return mSamples[(oldestIndex + index) % kCapacity];
}

const FrameTelemetrySample* TelemetryRingBuffer::latestSample() const noexcept {
    return (mCount > 0) ? &sampleNewestFirst(0) : nullptr;
}

const FrameTelemetrySample& TelemetryRingBuffer::sampleNewestFirst(size_t age) const noexcept {
    assert(age < mCount);
    return mSamples[(mHead + kCapacity - 1 - age) % kCapacity];
}

float TelemetryRingBuffer::computeSwapDurationMs() const noexcept {
    if (mCount == 0) {
        return 16.67f;
    }
    const size_t swapDurationWindow = std::min(mCount, kSwapDurationWindowFrames);
    uint64_t durationSumNs = 0;
    for (size_t i = 0; i < swapDurationWindow; ++i) {
        durationSumNs += sampleNewestFirst(i).frameDurationNs;
    }
    const float averageDurationMs =
            (static_cast<float>(durationSumNs) / static_cast<float>(swapDurationWindow)) /
            1000000.0f;
    if (averageDurationMs <= 20.0f) {
        return 16.67f;
    }
    if (averageDurationMs <= 29.0f) {
        return 25.00f;
    }
    if (averageDurationMs <= 42.0f) {
        return 33.33f;
    }
    if (averageDurationMs <= 58.0f) {
        return 50.00f;
    }
    return 66.67f;
}

TelemetryRollingStats TelemetryRingBuffer::computeRollingStats() const noexcept {
    TelemetryRollingStats rolling{};
    rolling.swapDurationMs = computeSwapDurationMs();
    rolling.clusterCount = mClusterCount;

    std::array<int32_t, kMaxClusters> clusterFrameCounts{};
    std::array<float, kMaxClusters> clusterFrequencySums{};
    int32_t validCoreSamples = 0;
    float gpuFrequencySum = 0.0f;
    int32_t validGpuSamples = 0;
    int32_t latestClusterIndex = -1;

    if (mCount > 0) {
        latestClusterIndex = sampleNewestFirst(0).cpuClusterIndex;

        const size_t rollingWindow = std::min(mCount, kRollingFrequencyWindowFrames);
        for (size_t i = 0; i < rollingWindow; ++i) {
            const FrameTelemetrySample& sample = sampleNewestFirst(i);
            if (sample.cpuClusterIndex >= 0 && sample.cpuClusterIndex < mClusterCount) {
                clusterFrameCounts[sample.cpuClusterIndex]++;
                clusterFrequencySums[sample.cpuClusterIndex] += sample.cpuFrequencyPercent;
                validCoreSamples++;
            }
            if (sample.gpuFrequencyPercent >= 0.0f) {
                gpuFrequencySum += sample.gpuFrequencyPercent;
                validGpuSamples++;
            }
        }
    }

    for (size_t clusterIndex = 0; clusterIndex < kMaxClusters; ++clusterIndex) {
        ClusterRollingStats& stats = rolling.clusterStats[clusterIndex];
        const int32_t framesInCluster = clusterFrameCounts[clusterIndex];
        stats.averageFrequencyPercent = (framesInCluster > 0)
                ? (clusterFrequencySums[clusterIndex] / static_cast<float>(framesInCluster))
                : 0.0f;
        stats.residencyPercent = (validCoreSamples > 0)
                ? (static_cast<float>(framesInCluster) * 100.0f /
                   static_cast<float>(validCoreSamples))
                : 0.0f;
        stats.isActiveCluster = (static_cast<int32_t>(clusterIndex) == latestClusterIndex);
    }
    rolling.averageGpuFrequencyPercent =
            (validGpuSamples > 0) ? (gpuFrequencySum / static_cast<float>(validGpuSamples)) : -1.0f;
    return rolling;
}

Histogram1sData TelemetryRingBuffer::computeHistogram1s() const noexcept {
    Histogram1sData hist{};
    int64_t windowElapsedNs = 0;
    float sumExp = 0.0f;
    float sumCpu = 0.0f;
    float sumGl = 0.0f;
    float sumTot = 0.0f;

    for (size_t i = 0; i < mCount; ++i) {
        const FrameTelemetrySample& s = sampleNewestFirst(i);
        if (s.frameDurationNs == 0) {
            continue;
        }

        windowElapsedNs += static_cast<int64_t>(s.frameDurationNs);
        hist.windowFrames++;

        const float totalMs = static_cast<float>(s.frameDurationNs) / 1000000.0f;
        const float cpuMs = s.realCpuMs;
        const float glMs = s.gpuWorkMs;
        const float workMs = cpuMs + glMs;
        const float expMs = s.expectedWorkMs;

        sumTot += totalMs;
        sumCpu += cpuMs;
        sumGl += glMs;
        sumExp += expMs;

        if (totalMs <= 18.5f) {
            hist.frames60++;
        } else if (totalMs <= 28.5f) {
            hist.frames40++;
        } else if (totalMs <= 38.5f) {
            hist.frames30++;
        } else {
            hist.framesSlow++;
        }

        const size_t tBin =
                std::clamp(static_cast<size_t>(totalMs), size_t{0}, Histogram1sData::kBinCount - 1);
        hist.totalBins[tBin]++;

        if (workMs > 0.05f) {
            const size_t wBin = std::clamp(static_cast<size_t>(workMs), size_t{0},
                                           Histogram1sData::kBinCount - 1);
            hist.workBins[wBin]++;
            hist.workCpuSums[wBin] += cpuMs;
            hist.workTotalSums[wBin] += workMs;
        }

        if (expMs > 0.05f) {
            const size_t eBin = std::clamp(static_cast<size_t>(expMs), size_t{0},
                                           Histogram1sData::kBinCount - 1);
            hist.expBins[eBin]++;
        }

        if (windowElapsedNs >= 1000000000LL) {
            break;
        }
    }

    if (hist.windowFrames > 0) {
        const float countF = static_cast<float>(hist.windowFrames);
        hist.avgExpMs = sumExp / countF;
        hist.avgCpuMs = sumCpu / countF;
        hist.avgGlMs = sumGl / countF;
        hist.avgTotalMs = sumTot / countF;
    }
    return hist;
}

} // namespace samples
