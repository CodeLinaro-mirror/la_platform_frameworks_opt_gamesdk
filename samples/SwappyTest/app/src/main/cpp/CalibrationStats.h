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

// Least-squares fits that turn calibration samples into a workload rate.

#include <stddef.h>

#include <vector>

namespace dvfs {

struct LinearRegressionStats {
    double slope = 0.0;
    double intercept = 0.0;
    double r_squared = 0.0;
    double slope_std_err = 0.0;
    double rme95_pct = 0.0;
    double mean_error_pct = 0.0;
    int n = 0;
    bool valid = false;
};

struct SlidingWindowRegressionStats {
    LinearRegressionStats ols;
    double plateau_drift_pct = 100.0;
    size_t window_start = 0;
    size_t window_size = 0;
};

LinearRegressionStats compute_linear_regression(const std::vector<double>& x,
                                                const std::vector<double>& y);

SlidingWindowRegressionStats compute_sliding_window_regression(const std::vector<double>& x,
                                                               const std::vector<double>& y,
                                                               size_t window_samples);

} // namespace dvfs
