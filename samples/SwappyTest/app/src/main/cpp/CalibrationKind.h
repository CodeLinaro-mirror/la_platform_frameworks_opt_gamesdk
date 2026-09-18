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

#include <stdint.h>

// The independent calibrations, each run by its own action and stored in its
// own cache: the CPU workload rate, and the GPU workload rate of each render
// API. SwappyTestActivity mirrors these values.
typedef enum {
    CALIBRATION_CPU = 0,
    CALIBRATION_GLES = 1,
    CALIBRATION_VULKAN = 2,
    CALIBRATION_KIND_COUNT = 3
} CalibrationKind;

// A set of CalibrationKind values: bit (1 << kind) per kind.
typedef uint32_t CalibrationSet;

// The state of a calibration cache. SwappyTestActivity mirrors these values.
typedef enum {
    CALIBRATION_STATUS_MISSING = 0,
    // Present but unusable: obsolete, or for another build or surface size.
    CALIBRATION_STATUS_INVALID = 1,
    CALIBRATION_STATUS_VALID = 2
} CalibrationStatus;

#ifdef __cplusplus

#include <string>

namespace dvfs {

constexpr CalibrationSet calibration_bit(CalibrationKind kind) {
    return 1u << kind;
}

constexpr CalibrationSet kAllCalibrations = (1u << CALIBRATION_KIND_COUNT) - 1;

// "cpu", "gles" or "vulkan": the name in cache files, logs and run records.
inline const char* calibration_kind_name(CalibrationKind kind) {
    switch (kind) {
        case CALIBRATION_CPU:
            return "cpu";
        case CALIBRATION_GLES:
            return "gles";
        case CALIBRATION_VULKAN:
            return "vulkan";
        default:
            return "unknown";
    }
}

// The kinds in set as a comma-separated list such as "cpu,vulkan".
inline std::string format_calibration_set(CalibrationSet set) {
    std::string out;
    for (int k = 0; k < CALIBRATION_KIND_COUNT; ++k) {
        const CalibrationKind kind = static_cast<CalibrationKind>(k);
        if ((set & calibration_bit(kind)) == 0) continue;
        if (!out.empty()) out += ",";
        out += calibration_kind_name(kind);
    }
    return out;
}

} // namespace dvfs

#endif
