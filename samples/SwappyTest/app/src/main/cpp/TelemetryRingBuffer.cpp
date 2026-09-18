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
#include <cmath>
#include <cstdio>
#include <cstring>

#include "DvfsBenchmark.h"

namespace samples {

void TelemetryRingBuffer::Reset() noexcept {
    mHead = 0;
    mCount = 0;
    mActiveCadenceMs = 16.67f;
    mClusterStats.fill({});
    mAverageGpuFrequencyPercent = -1.0f;
    mLineExpectedFps.fill('\0');
    mLineRealFps.fill('\0');
    mLineCpuFrequency.fill('\0');
    mLineTestStatus.fill('\0');
}

void TelemetryRingBuffer::RecordFrame(
        uint64_t frameDurationNs, float gpuWorkMs, const char* pacingStrategyName,
        const std::array<bool, TelemetrySnapshot::kNumFpsGuidelines>& supportedFps) noexcept {
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
    if (detectedClusters >= 1 &&
        detectedClusters <= static_cast<int32_t>(TelemetrySnapshot::kMaxClusters)) {
        mClusterCount = detectedClusters;
    }

    mSupportedFps = supportedFps;
    mHead = (mHead + 1) % kCapacity;
    if (mCount < kCapacity) {
        ++mCount;
    }

    UpdateRollingStatsAndText(ui, pacingStrategyName);
}

void TelemetryRingBuffer::UpdateRollingStatsAndText(const DvfsUiSnapshot& ui,
                                                    const char* pacingStrategyName) noexcept {
    if (mCount == 0) {
        mActiveCadenceMs = 16.67f;
    } else {
        const size_t cadenceWindow = std::min(mCount, kActiveCadenceWindowFrames);
        uint64_t durationSumNs = 0;
        for (size_t i = 0; i < cadenceWindow; ++i) {
            const size_t sampleIndex = (mHead + kCapacity - 1 - i) % kCapacity;
            durationSumNs += mSamples[sampleIndex].frameDurationNs;
        }
        const float averageDurationMs =
                (static_cast<float>(durationSumNs) / static_cast<float>(cadenceWindow)) /
                1000000.0f;
        if (averageDurationMs <= 20.0f) {
            mActiveCadenceMs = 16.67f;
        } else if (averageDurationMs <= 29.0f) {
            mActiveCadenceMs = 25.00f;
        } else if (averageDurationMs <= 42.0f) {
            mActiveCadenceMs = 33.33f;
        } else if (averageDurationMs <= 58.0f) {
            mActiveCadenceMs = 50.00f;
        } else {
            mActiveCadenceMs = 66.67f;
        }
    }

    std::array<int32_t, TelemetrySnapshot::kMaxClusters> clusterFrameCounts{};
    std::array<float, TelemetrySnapshot::kMaxClusters> clusterFrequencySums{};
    int32_t validCoreSamples = 0;
    float gpuFrequencySum = 0.0f;
    int32_t validGpuSamples = 0;
    int32_t latestClusterIndex = -1;

    if (mCount > 0) {
        const size_t latestIndex = (mHead + kCapacity - 1) % kCapacity;
        latestClusterIndex = mSamples[latestIndex].cpuClusterIndex;

        const size_t rollingWindow = std::min(mCount, kRollingFrequencyWindowFrames);
        for (size_t i = 0; i < rollingWindow; ++i) {
            const size_t sampleIndex = (mHead + kCapacity - 1 - i) % kCapacity;
            const FrameTelemetrySample& sample = mSamples[sampleIndex];
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

    for (size_t clusterIndex = 0; clusterIndex < TelemetrySnapshot::kMaxClusters; ++clusterIndex) {
        ClusterRollingStats& stats = mClusterStats[clusterIndex];
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
    mAverageGpuFrequencyPercent =
            (validGpuSamples > 0) ? (gpuFrequencySum / static_cast<float>(validGpuSamples)) : -1.0f;

    const int32_t phase = ui.test_phase;
    const char* apiName = get_active_render_api_name();
    const char* scenarioName = ui.test_scenario;
    const float expectedFps = ui.expected_fps;
    const float realFps = ui.measured_real_fps;
    const size_t latestIndex = (mCount > 0) ? ((mHead + kCapacity - 1) % kCapacity) : 0;
    const float latestCpuMs = (mCount > 0) ? mSamples[latestIndex].realCpuMs : 0.0f;
    const float latestGpuMs = (mCount > 0) ? mSamples[latestIndex].gpuWorkMs : 0.0f;

    if (phase == TEST_PHASE_READY) {
        std::snprintf(mLineExpectedFps.data(), mLineExpectedFps.size(), "[%s] Exp: Ready [%s]",
                      apiName, scenarioName);
    } else if (phase == TEST_PHASE_COOLDOWN) {
        std::snprintf(mLineExpectedFps.data(), mLineExpectedFps.size(), "[%s] Exp: Cooldown [%s]",
                      apiName, scenarioName);
    } else {
        std::snprintf(mLineExpectedFps.data(), mLineExpectedFps.size(), "[%s] Exp: %.1f FPS [%s]",
                      apiName, expectedFps, scenarioName);
    }

    const bool runningScenarioSteps =
            (phase == TEST_PHASE_SPIKE_27MS || phase == TEST_PHASE_RAMP_DOWN);
    if (runningScenarioSteps && (latestCpuMs > 0.0f || latestGpuMs > 0.0f)) {
        std::snprintf(mLineRealFps.data(), mLineRealFps.size(),
                      "Real: %.1f FPS (%.1f+%.1fms) | Pacing: %s", realFps, latestCpuMs,
                      latestGpuMs, pacingStrategyName ? pacingStrategyName : "auto");
    } else {
        std::snprintf(mLineRealFps.data(), mLineRealFps.size(), "Real: %.1f FPS | Pacing: %s",
                      realFps, pacingStrategyName ? pacingStrategyName : "auto");
    }

    const int32_t cpuCore = ui.current_cpu_core;
    const int32_t cpuFreqMhz = ui.current_cpu_freq_mhz;
    const int32_t cpuMaxFreqMhz = ui.current_cpu_max_freq_mhz;
    const int32_t deviceMaxMhz = ui.device_global_max_freq_mhz;
    const int32_t calibFreqMhz = ui.calib_freq_mhz;
    const bool calibAtMax = ui.calib_at_max_freq;
    const bool runTainted = ui.run_tainted;

    char calibTag[48];
    if (runTainted) {
        std::snprintf(calibTag, sizeof(calibTag), " [TAINTED]");
    } else if (runningScenarioSteps && !calibAtMax && deviceMaxMhz > 0) {
        std::snprintf(calibTag, sizeof(calibTag), " [THROTTLED %d/%d]", calibFreqMhz, deviceMaxMhz);
    } else {
        std::snprintf(calibTag, sizeof(calibTag), " [Calib OK]");
    }

    if (cpuFreqMhz > 0) {
        const int32_t referenceMaxMhz = std::max(cpuMaxFreqMhz, cpuFreqMhz);
        const float clockPercent = (referenceMaxMhz > 0)
                ? (static_cast<float>(cpuFreqMhz) * 100.0f / static_cast<float>(referenceMaxMhz))
                : 0.0f;
        std::snprintf(mLineCpuFrequency.data(), mLineCpuFrequency.size(),
                      "CPU: Core %d @ %d/%d MHz (%.0f%%)%s", cpuCore, cpuFreqMhz, referenceMaxMhz,
                      clockPercent, calibTag);
    } else {
        std::snprintf(mLineCpuFrequency.data(), mLineCpuFrequency.size(),
                      "CPU: Core %d (eff %.0f%%)%s", cpuCore,
                      std::min(100.0f, ui.cpu_freq_ratio_pct), calibTag);
    }

    const int32_t stepIndex = ui.current_step_index;
    const int32_t stepCount = ui.scenario_step_count;
    char stepPrefix[32] = "";
    if (stepIndex >= 0 && stepCount > 0) {
        std::snprintf(stepPrefix, sizeof(stepPrefix), "Step %d/%d: ", stepIndex + 1, stepCount);
    }

    switch (phase) {
        case TEST_PHASE_READY:
            std::snprintf(mLineTestStatus.data(), mLineTestStatus.size(), "Status: Ready");
            break;
        case TEST_PHASE_COOLDOWN:
            std::snprintf(mLineTestStatus.data(), mLineTestStatus.size(),
                          "Cooldown: %d%% headroom (%.1fs)", ui.cooldown_headroom_pct,
                          ui.cooldown_progress_sec);
            break;
        case TEST_PHASE_SPIKE_27MS:
            std::snprintf(mLineTestStatus.data(), mLineTestStatus.size(),
                          "%sSpike 27 ms (%.1fs left)", stepPrefix, ui.step_time_remaining_sec);
            break;
        case TEST_PHASE_RAMP_DOWN:
            std::snprintf(mLineTestStatus.data(), mLineTestStatus.size(),
                          "%sTarget: %.0f ms (%.1fs left)%s", stepPrefix, ui.target_workload_ms,
                          ui.step_time_remaining_sec, ui.fps_diverged ? " [DIVERGED]" : "");
            break;
        case TEST_PHASE_COMPLETE:
        default: {
            if (ui.run_tainted) {
                std::snprintf(mLineTestStatus.data(), mLineTestStatus.size(),
                              "Complete: TAINTED (%s)", ui.taint_reason);
            } else {
                std::snprintf(mLineTestStatus.data(), mLineTestStatus.size(), "Complete");
            }
            break;
        }
    }
}

void TelemetryRingBuffer::CaptureSnapshot(TelemetrySnapshot* outSnapshot) const noexcept {
    assert(outSnapshot != nullptr);
    outSnapshot->samples = mSamples;
    outSnapshot->sampleCount = mCount;
    outSnapshot->oldestSampleIndex = (mCount < kCapacity) ? 0 : mHead;
    outSnapshot->activeCadenceMs = mActiveCadenceMs;
    outSnapshot->supportedFps = mSupportedFps;
    outSnapshot->clusterCount = mClusterCount;
    outSnapshot->clusterStats = mClusterStats;
    outSnapshot->averageGpuFrequencyPercent = mAverageGpuFrequencyPercent;
    outSnapshot->lineExpectedFps = mLineExpectedFps;
    outSnapshot->lineRealFps = mLineRealFps;
    outSnapshot->lineCpuFrequency = mLineCpuFrequency;
    outSnapshot->lineTestStatus = mLineTestStatus;

    TelemetrySnapshot::Histogram1sData hist{};
    int64_t windowElapsedNs = 0;
    float sumExp = 0.0f;
    float sumCpu = 0.0f;
    float sumGl = 0.0f;
    float sumTot = 0.0f;

    for (size_t i = 0; i < mCount; ++i) {
        const size_t idx = (mHead + kCapacity - 1 - i) % kCapacity;
        const FrameTelemetrySample& s = mSamples[idx];
        if (s.frameDurationNs == 0) continue;

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

        const size_t tBin = std::clamp(static_cast<size_t>(totalMs), size_t{0},
                                       TelemetrySnapshot::Histogram1sData::kBinCount - 1);
        hist.totalBins[tBin]++;

        if (workMs > 0.05f) {
            const size_t wBin = std::clamp(static_cast<size_t>(workMs), size_t{0},
                                           TelemetrySnapshot::Histogram1sData::kBinCount - 1);
            hist.workBins[wBin]++;
            hist.workCpuSums[wBin] += cpuMs;
            hist.workTotalSums[wBin] += workMs;
        }

        if (expMs > 0.05f) {
            const size_t eBin = std::clamp(static_cast<size_t>(expMs), size_t{0},
                                           TelemetrySnapshot::Histogram1sData::kBinCount - 1);
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
    outSnapshot->histogram = hist;
}

} // namespace samples
