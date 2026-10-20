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

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "DvfsBenchmark.h"
#include "TelemetryRingBuffer.h"

namespace {

DvfsUiSnapshot g_test_ui_snapshot{};

} // namespace

extern "C" {

void get_ui_snapshot(DvfsUiSnapshot* out) {
    if (out != nullptr) {
        *out = g_test_ui_snapshot;
    }
}

const char* get_active_render_api_name(void) {
    return "gles";
}

} // extern "C"

namespace samples {
namespace {

void RecordTestFrames(TelemetryRingBuffer& ring, size_t count, uint64_t frameDurationNs,
                      float realCpuMs, float gpuWorkMs) {
    g_test_ui_snapshot.last_frame_real_cpu_ms = realCpuMs;
    for (size_t i = 0; i < count; ++i) {
        ring.recordFrame(frameDurationNs, gpuWorkMs);
    }
}

TEST(TelemetryRingBufferTest, ComputeHistogram1sBinsWorkByCombinedCpuAndGpuMs) {
    TelemetryRingBuffer ring;

    // Frame 1: CPU-only (6.2 ms CPU, 0.0 ms GPU -> workMs = 6.2 -> bin 6).
    g_test_ui_snapshot = {};
    g_test_ui_snapshot.last_frame_expected_ms = 6.0f;
    g_test_ui_snapshot.last_frame_real_cpu_ms = 6.2f;
    ring.recordFrame(16666667ULL, /*gpuWorkMs=*/0.0f);

    // Frame 2: GPU-only (0.0 ms CPU, 11.4 ms GPU -> workMs = 11.4 -> bin 11).
    g_test_ui_snapshot.last_frame_expected_ms = 11.0f;
    g_test_ui_snapshot.last_frame_real_cpu_ms = 0.0f;
    ring.recordFrame(16666667ULL, /*gpuWorkMs=*/11.4f);

    // Frame 3: Combined CPU + GPU (4.3 ms CPU, 9.2 ms GPU -> workMs = 13.5 -> bin 13).
    g_test_ui_snapshot.last_frame_expected_ms = 13.0f;
    g_test_ui_snapshot.last_frame_real_cpu_ms = 4.3f;
    ring.recordFrame(16666667ULL, /*gpuWorkMs=*/9.2f);

    const Histogram1sData hist = ring.computeHistogram1s();

    EXPECT_EQ(hist.windowFrames, 3);
    EXPECT_EQ(hist.workBins[6], 1);
    EXPECT_FLOAT_EQ(hist.workCpuSums[6], 6.2f);
    EXPECT_FLOAT_EQ(hist.workTotalSums[6], 6.2f);

    EXPECT_EQ(hist.workBins[11], 1);
    EXPECT_FLOAT_EQ(hist.workCpuSums[11], 0.0f);
    EXPECT_FLOAT_EQ(hist.workTotalSums[11], 11.4f);

    EXPECT_EQ(hist.workBins[13], 1);
    EXPECT_FLOAT_EQ(hist.workCpuSums[13], 4.3f);
    EXPECT_NEAR(hist.workTotalSums[13], 13.5f, 1e-5f);
    EXPECT_EQ(hist.workBins[4], 0);
}

TEST(TelemetryRingBufferTest, SampleAccessorsTrackOldestAndLatestAcrossWraparound) {
    TelemetryRingBuffer ring;
    EXPECT_EQ(ring.sampleCount(), 0u);
    EXPECT_EQ(ring.latestSample(), nullptr);

    g_test_ui_snapshot = {};
    RecordTestFrames(ring, /*count=*/1, /*frameDurationNs=*/16666667ULL, /*realCpuMs=*/3.5f,
                     /*gpuWorkMs=*/1.5f);
    RecordTestFrames(ring, /*count=*/TelemetryRingBuffer::kCapacity - 1,
                     /*frameDurationNs=*/16666667ULL, /*realCpuMs=*/5.0f, /*gpuWorkMs=*/2.0f);

    EXPECT_EQ(ring.sampleCount(), TelemetryRingBuffer::kCapacity);
    EXPECT_FLOAT_EQ(ring.sampleOldestFirst(0).realCpuMs, 3.5f);
    ASSERT_NE(ring.latestSample(), nullptr);
    EXPECT_FLOAT_EQ(ring.latestSample()->realCpuMs, 5.0f);

    // Recording one more sample overwrites the oldest (3.5 ms) and advances the latest to 9.25 ms.
    RecordTestFrames(ring, /*count=*/1, /*frameDurationNs=*/33333333ULL, /*realCpuMs=*/9.25f,
                     /*gpuWorkMs=*/4.5f);

    EXPECT_EQ(ring.sampleCount(), TelemetryRingBuffer::kCapacity);
    EXPECT_FLOAT_EQ(ring.sampleOldestFirst(0).realCpuMs, 5.0f);
    EXPECT_FLOAT_EQ(ring.sampleOldestFirst(TelemetryRingBuffer::kCapacity - 1).realCpuMs, 9.25f);
    ASSERT_NE(ring.latestSample(), nullptr);
    EXPECT_FLOAT_EQ(ring.latestSample()->realCpuMs, 9.25f);
    EXPECT_FLOAT_EQ(ring.latestSample()->gpuWorkMs, 4.5f);

    ring.reset();
    EXPECT_EQ(ring.sampleCount(), 0u);
    EXPECT_EQ(ring.latestSample(), nullptr);
}

TEST(TelemetryRingBufferTest, ComputeRollingStatsAggregatesSwapDurationAndClusterResidency) {
    TelemetryRingBuffer ring;
    g_test_ui_snapshot = {};
    g_test_ui_snapshot.cpu_cluster_count = 3;

    // 1 frame on cluster 0 (40% CPU, 50% GPU) and 3 frames on cluster 2 (80% CPU, 70% GPU)
    // at 33.3 ms frame duration (30 FPS swap duration bucket).
    g_test_ui_snapshot.current_cpu_cluster = 0;
    g_test_ui_snapshot.cpu_freq_pct = 40.0f;
    g_test_ui_snapshot.gpu_freq_pct = 50.0f;
    RecordTestFrames(ring, /*count=*/1, /*frameDurationNs=*/33333333ULL, /*realCpuMs=*/10.0f,
                     /*gpuWorkMs=*/5.0f);

    g_test_ui_snapshot.current_cpu_cluster = 2;
    g_test_ui_snapshot.cpu_freq_pct = 80.0f;
    g_test_ui_snapshot.gpu_freq_pct = 70.0f;
    RecordTestFrames(ring, /*count=*/3, /*frameDurationNs=*/33333333ULL, /*realCpuMs=*/12.0f,
                     /*gpuWorkMs=*/6.0f);

    const TelemetryRollingStats rolling = ring.computeRollingStats();

    EXPECT_FLOAT_EQ(rolling.swapDurationMs, 33.33f);
    EXPECT_EQ(rolling.clusterCount, 3);
    EXPECT_FLOAT_EQ(rolling.clusterStats[0].averageFrequencyPercent, 40.0f);
    EXPECT_FLOAT_EQ(rolling.clusterStats[0].residencyPercent, 25.0f);
    EXPECT_FALSE(rolling.clusterStats[0].isActiveCluster);

    EXPECT_FLOAT_EQ(rolling.clusterStats[2].averageFrequencyPercent, 80.0f);
    EXPECT_FLOAT_EQ(rolling.clusterStats[2].residencyPercent, 75.0f);
    EXPECT_TRUE(rolling.clusterStats[2].isActiveCluster);

    EXPECT_FLOAT_EQ(rolling.averageGpuFrequencyPercent, 65.0f);
}

} // namespace
} // namespace samples
