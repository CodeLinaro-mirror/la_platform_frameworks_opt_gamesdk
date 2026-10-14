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

#include <cmath>
#include <vector>

#include "CalibrationStats.h"

namespace dvfs {
namespace {

// ============================================================================
// CalibrationStats regression and sliding-window convergence tests
// ============================================================================

TEST(CalibrationStatsTest, LinearRegressionExactLine) {
    // y = 1.25 + 0.05 * x
    const std::vector<double> x = {100.0, 200.0, 300.0, 400.0, 500.0, 600.0};
    std::vector<double> y;
    for (double xi : x) {
        y.push_back(1.25 + 0.05 * xi);
    }

    const LinearRegressionStats stats = compute_linear_regression(x, y);
    ASSERT_TRUE(stats.valid);
    EXPECT_EQ(stats.n, 6);
    EXPECT_NEAR(stats.slope, 0.05, 1e-12);
    EXPECT_NEAR(stats.intercept, 1.25, 1e-12);
    EXPECT_NEAR(stats.r_squared, 1.0, 1e-12);
    EXPECT_NEAR(stats.rme95_pct, 0.0, 1e-9);
    EXPECT_NEAR(stats.mean_error_pct, 0.0, 1e-9);
}

TEST(CalibrationStatsTest, LinearRegressionDegenerateInputs) {
    EXPECT_FALSE(compute_linear_regression({}, {}).valid);
    EXPECT_FALSE(compute_linear_regression({1.0, 2.0}, {2.0, 4.0}).valid);
    EXPECT_FALSE(compute_linear_regression({1.0, 2.0, 3.0}, {2.0, 4.0}).valid);
    // Zero variance in x (vertical line).
    EXPECT_FALSE(compute_linear_regression({5.0, 5.0, 5.0, 5.0}, {1.0, 2.0, 3.0, 4.0}).valid);
}

TEST(CalibrationStatsTest, SlidingWindowRegressionExactFitAndOutlierRejection) {
    // 16 samples across 4 sweeps of {100, 200, 350, 500} following y = 2.0 + 0.04 * x.
    const double levels[4] = {100.0, 200.0, 350.0, 500.0};
    std::vector<double> x;
    std::vector<double> y;
    for (int sweep = 0; sweep < 4; ++sweep) {
        for (double lv : levels) {
            x.push_back(lv);
            y.push_back(2.0 + 0.04 * lv);
        }
    }

    const SlidingWindowRegressionStats clean_win =
            compute_sliding_window_regression(x, y, /*window_samples=*/16);
    ASSERT_TRUE(clean_win.ols.valid);
    EXPECT_EQ(clean_win.window_start, 0u);
    EXPECT_EQ(clean_win.window_size, 16u);
    EXPECT_NEAR(clean_win.ols.slope, 0.04, 1e-12);
    EXPECT_NEAR(clean_win.ols.intercept, 2.0, 1e-12);
    EXPECT_NEAR(clean_win.ols.r_squared, 1.0, 1e-12);
    EXPECT_NEAR(clean_win.ols.rme95_pct, 0.0, 1e-9);
    EXPECT_NEAR(clean_win.plateau_drift_pct, 0.0, 1e-9);

    // Inject a single OS preemption spike (+45 ms) at sample 5; the 4-bin median
    // MAD filter drops the isolated spike and recovers the exact underlying fit.
    std::vector<double> spiked_y = y;
    spiked_y[5] += 45.0;
    const SlidingWindowRegressionStats spiked_win =
            compute_sliding_window_regression(x, spiked_y, /*window_samples=*/16);
    ASSERT_TRUE(spiked_win.ols.valid);
    EXPECT_EQ(spiked_win.ols.n, 15);
    EXPECT_NEAR(spiked_win.ols.slope, 0.04, 1e-9);
    EXPECT_NEAR(spiked_win.ols.intercept, 2.0, 1e-9);
    EXPECT_NEAR(spiked_win.ols.r_squared, 1.0, 1e-9);
}

TEST(CalibrationStatsTest, SlidingWindowRegressionHighNoiseAndPlateauDrift) {
    const double levels[4] = {100.0, 200.0, 350.0, 500.0};

    // High-noise alternating perturbation fails R^2 and RME95 convergence gates.
    std::vector<double> x;
    std::vector<double> noisy_y;
    const double noise[4] = {-6.0, 7.5, -8.0, 9.0};
    for (int sweep = 0; sweep < 4; ++sweep) {
        for (int i = 0; i < 4; ++i) {
            x.push_back(levels[i]);
            noisy_y.push_back(2.0 + 0.04 * levels[i] + noise[(sweep + i) % 4]);
        }
    }
    const SlidingWindowRegressionStats noisy_win =
            compute_sliding_window_regression(x, noisy_y, /*window_samples=*/16);
    ASSERT_TRUE(noisy_win.ols.valid);
    EXPECT_LT(noisy_win.ols.r_squared, 0.90);
    EXPECT_GT(noisy_win.ols.rme95_pct, 10.0);

    // Plateau drift: first half runs at slope 0.040 (25.0 iters/ms), second half
    // drifts to slope 0.042 (23.81 iters/ms -> exact 5.0% throughput drift,
    // exceeding the 2.5% CPU convergence threshold).
    std::vector<double> drift_y;
    for (int sweep = 0; sweep < 4; ++sweep) {
        const double slope = (sweep < 2) ? 0.040 : 0.042;
        for (double lv : levels) {
            drift_y.push_back(1.0 + slope * lv);
        }
    }
    const SlidingWindowRegressionStats drift_win =
            compute_sliding_window_regression(x, drift_y, /*window_samples=*/16);
    ASSERT_TRUE(drift_win.ols.valid);
    EXPECT_NEAR(drift_win.plateau_drift_pct, 5.0, 1e-9);
}

TEST(CalibrationStatsTest, SlidingWindowRegressionSlicesRecentWindowAndSmallInputs) {
    EXPECT_FALSE(compute_sliding_window_regression({1.0, 2.0, 3.0}, {2.0, 4.0, 6.0}, 16).ols.valid);
    // Mismatched x/y vector sizes and undersized window_samples (< 4) are rejected.
    EXPECT_FALSE(
            compute_sliding_window_regression({1.0, 2.0, 3.0, 4.0, 5.0}, {2.0, 4.0, 6.0, 8.0}, 4)
                    .ols.valid);
    EXPECT_FALSE(compute_sliding_window_regression({1.0, 2.0, 3.0, 4.0}, {2.0, 4.0, 6.0, 8.0}, 3)
                         .ols.valid);
    EXPECT_FALSE(compute_sliding_window_regression({1.0, 2.0, 3.0, 4.0}, {2.0, 4.0, 6.0, 8.0}, 0)
                         .ols.valid);

    // 8 noisy warmup samples followed by 16 exact linear samples: window=16
    // slices only the trailing 16 samples.
    std::vector<double> x = {10, 20, 30, 40, 50, 60, 70, 80};
    std::vector<double> y = {99, 12, 85, 3, 77, 41, 19, 63};
    for (int i = 0; i < 16; ++i) {
        const double xi = 100.0 + 25.0 * i;
        x.push_back(xi);
        y.push_back(0.5 + 0.1 * xi);
    }
    const SlidingWindowRegressionStats win = compute_sliding_window_regression(x, y, 16);
    ASSERT_TRUE(win.ols.valid);
    EXPECT_EQ(win.window_start, 8u);
    EXPECT_EQ(win.window_size, 16u);
    EXPECT_NEAR(win.ols.slope, 0.1, 1e-12);
    EXPECT_NEAR(win.ols.intercept, 0.5, 1e-12);
    EXPECT_NEAR(win.ols.r_squared, 1.0, 1e-12);
}

TEST(CalibrationStatsTest, SlidingWindowRegressionRecoversFromNegativeFourBinSlope) {
    // Upper 3 bins {350, 360, 370} follow y = 2.0 + 0.05 * x (19.5, 20.0, 20.5 ms),
    // while the lowest bin (x = 100) is spiked to 60.0 ms so the 4-bin base_fit has
    // a negative slope with high R^2 (> 0.995). The 3-bin up_fit recovery must still
    // trigger on the negative slope, drop the 4 bin-0 outliers, and recover the exact
    // 3-level positive linear fit.
    const double levels[4] = {100.0, 350.0, 360.0, 370.0};
    std::vector<double> x;
    std::vector<double> y;
    for (int sweep = 0; sweep < 4; ++sweep) {
        for (int i = 0; i < 4; ++i) {
            x.push_back(levels[i]);
            y.push_back(i == 0 ? 60.0 : (2.0 + 0.05 * levels[i]));
        }
    }
    const SlidingWindowRegressionStats win =
            compute_sliding_window_regression(x, y, /*window_samples=*/16);
    ASSERT_TRUE(win.ols.valid);
    EXPECT_EQ(win.ols.n, 12);
    EXPECT_NEAR(win.ols.slope, 0.05, 1e-9);
    EXPECT_NEAR(win.ols.intercept, 2.0, 1e-9);
    EXPECT_NEAR(win.ols.r_squared, 1.0, 1e-9);
}

} // namespace
} // namespace dvfs
