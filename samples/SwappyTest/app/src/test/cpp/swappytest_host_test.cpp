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

#include "CalibrationKind.h"

namespace dvfs {
namespace {

// ============================================================================
// CalibrationKind tests
// ============================================================================

TEST(CalibrationKindTest, BitAndMaskConstants) {
    EXPECT_EQ(calibration_bit(CALIBRATION_CPU), 1u);
    EXPECT_EQ(calibration_bit(CALIBRATION_GLES), 2u);
    EXPECT_EQ(calibration_bit(CALIBRATION_VULKAN), 4u);
    EXPECT_EQ(kAllCalibrations, 7u);
}

TEST(CalibrationKindTest, KindNames) {
    EXPECT_STREQ(calibration_kind_name(CALIBRATION_CPU), "cpu");
    EXPECT_STREQ(calibration_kind_name(CALIBRATION_GLES), "gles");
    EXPECT_STREQ(calibration_kind_name(CALIBRATION_VULKAN), "vulkan");
    EXPECT_STREQ(calibration_kind_name(CALIBRATION_KIND_COUNT), "unknown");
    EXPECT_STREQ(calibration_kind_name(static_cast<CalibrationKind>(-1)), "unknown");
}

TEST(CalibrationKindTest, FormatCalibrationSet) {
    EXPECT_EQ(format_calibration_set(0), "");
    EXPECT_EQ(format_calibration_set(calibration_bit(CALIBRATION_CPU)), "cpu");
    EXPECT_EQ(format_calibration_set(calibration_bit(CALIBRATION_GLES)), "gles");
    EXPECT_EQ(format_calibration_set(calibration_bit(CALIBRATION_VULKAN)), "vulkan");
    EXPECT_EQ(format_calibration_set(calibration_bit(CALIBRATION_CPU) |
                                     calibration_bit(CALIBRATION_GLES)),
              "cpu,gles");
    EXPECT_EQ(format_calibration_set(calibration_bit(CALIBRATION_CPU) |
                                     calibration_bit(CALIBRATION_VULKAN)),
              "cpu,vulkan");
    EXPECT_EQ(format_calibration_set(calibration_bit(CALIBRATION_GLES) |
                                     calibration_bit(CALIBRATION_VULKAN)),
              "gles,vulkan");
    EXPECT_EQ(format_calibration_set(kAllCalibrations), "cpu,gles,vulkan");
    // Out-of-range bits above CALIBRATION_KIND_COUNT are ignored.
    EXPECT_EQ(format_calibration_set(calibration_bit(CALIBRATION_CPU) | (1u << 6)), "cpu");
}

} // namespace
} // namespace dvfs
