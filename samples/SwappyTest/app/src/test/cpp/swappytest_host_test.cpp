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

#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "CalibrationKind.h"
#include "CalibrationStats.h"
#include "DvfsBenchmark.h"
#include "Scenario.h"
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

// ============================================================================
// Scenario catalog, custom step parser, and calibration requirement tests
// ============================================================================

TEST(ScenarioTest, CatalogScenariosLookupAndMetadata) {
    const std::vector<std::string> kExpectedCatalogIds = {
            "S1", "S2", "S3", "S4", "S5", "S7", "S10", "S11", "H1",
            "H2", "H3", "H4", "H6", "H7", "H8", "H9",  "H10", "CUSTOM",
    };
    ASSERT_EQ(scenario_count(), static_cast<int>(kExpectedCatalogIds.size()));

    for (int i = 0; i < scenario_count(); ++i) {
        const ScenarioDef& def = scenario_at(i);
        ASSERT_NE(def.id, nullptr);
        ASSERT_NE(def.description, nullptr);
        EXPECT_EQ(kExpectedCatalogIds[i], def.id);
        EXPECT_EQ(find_scenario(def.id, /*ignore_case=*/false), &def);

        std::string lower_id = def.id;
        for (char& c : lower_id) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        EXPECT_EQ(find_scenario(lower_id.c_str(), /*ignore_case=*/true), &def);
        EXPECT_EQ(find_scenario(lower_id.c_str(), /*ignore_case=*/false), nullptr);

        if ((def.flags & kScenarioCustom) != 0) {
            continue;
        }

        const std::vector<ScenarioStep> steps = build_scenario_steps(def, "");
        ASSERT_FALSE(steps.empty()) << "Scenario " << def.id << " has no steps";
        EXPECT_LE(static_cast<int>(steps.size()), kMaxScenarioSteps);
        for (const ScenarioStep& step : steps) {
            EXPECT_GT(step.duration_sec, 0.0f) << "Scenario " << def.id;
        }

        const ScenarioTraits traits = scenario_traits(def, steps);
        EXPECT_TRUE(traits.uses_cpu) << "Scenario " << def.id;

        const bool expect_gpu = (std::string(def.id) == "S7" || std::string(def.id) == "H6" ||
                                 std::string(def.id) == "H9");
        EXPECT_EQ(traits.uses_gpu, expect_gpu) << "Scenario " << def.id;

        const bool expect_30fps = (std::string(def.id) == "S5" || std::string(def.id) == "H7");
        EXPECT_EQ(traits.explicit_30fps, expect_30fps) << "Scenario " << def.id;

        const bool expect_square_wave =
                (std::string(def.id) == "S3" || std::string(def.id) == "H3");
        EXPECT_EQ(traits.square_wave, expect_square_wave) << "Scenario " << def.id;

        const bool expect_boost = (std::string(def.id) == "H10");
        EXPECT_EQ(traits.needs_boost, expect_boost) << "Scenario " << def.id;
    }

    EXPECT_EQ(find_scenario(nullptr, /*ignore_case=*/true), nullptr);
    EXPECT_EQ(find_scenario("UNKNOWN_SCENARIO", /*ignore_case=*/true), nullptr);
}

TEST(ScenarioTest, CatalogSpecialStepProperties) {
    // S4 and H4 carry per-frame uniform random jitter bounds.
    const ScenarioDef* s4 = find_scenario("S4", false);
    ASSERT_NE(s4, nullptr);
    const std::vector<ScenarioStep> s4_steps = build_scenario_steps(*s4, "");
    ASSERT_EQ(s4_steps.size(), 6u);
    EXPECT_EQ(s4_steps[0].jitter_min_ms, 6);
    EXPECT_EQ(s4_steps[0].jitter_max_ms, 14);

    // S10 triggers Swappy resetFramePacing at the start of its fourth step.
    const ScenarioDef* s10 = find_scenario("S10", false);
    ASSERT_NE(s10, nullptr);
    const std::vector<ScenarioStep> s10_steps = build_scenario_steps(*s10, "");
    ASSERT_EQ(s10_steps.size(), 4u);
    EXPECT_FALSE(s10_steps[0].reset_pacing_at_start);
    EXPECT_FALSE(s10_steps[1].reset_pacing_at_start);
    EXPECT_FALSE(s10_steps[2].reset_pacing_at_start);
    EXPECT_TRUE(s10_steps[3].reset_pacing_at_start);
}

TEST(ScenarioTest, ParseCustomStepsMultiStepAndOverrides) {
    const std::vector<ScenarioStep> steps = parse_custom_steps(
            "3s:cpu=12,gpu=0,jit=6-14,pipe=off,auto_si=on,stuff_fix=2,fps=30; "
            "4.5s:cpu_ms=4,gpu_ms=14,pipeline=on,auto_swap_interval=false,reset=true,boost=1 | "
            "dur=6,cpu=0,gpu=8,auto_pipeline=auto,reset=off,boost=false");
    ASSERT_EQ(steps.size(), 3u);

    EXPECT_FLOAT_EQ(steps[0].duration_sec, 3.0f);
    EXPECT_EQ(steps[0].target_cpu_ms, 12);
    EXPECT_EQ(steps[0].target_gpu_ms, 0);
    EXPECT_EQ(steps[0].jitter_min_ms, 6);
    EXPECT_EQ(steps[0].jitter_max_ms, 14);
    EXPECT_EQ(steps[0].auto_pipeline, 0);
    EXPECT_EQ(steps[0].auto_swap_interval, 1);
    EXPECT_EQ(steps[0].buffer_stuffing_fix_wait, 2);
    EXPECT_EQ(steps[0].target_fps, 30);
    EXPECT_FALSE(steps[0].reset_pacing_at_start);
    EXPECT_FALSE(steps[0].external_boost_pulse);

    EXPECT_FLOAT_EQ(steps[1].duration_sec, 4.5f);
    EXPECT_EQ(steps[1].target_cpu_ms, 4);
    EXPECT_EQ(steps[1].target_gpu_ms, 14);
    EXPECT_EQ(steps[1].jitter_min_ms, 0);
    EXPECT_EQ(steps[1].jitter_max_ms, 0);
    EXPECT_EQ(steps[1].auto_pipeline, 1);
    EXPECT_EQ(steps[1].auto_swap_interval, 0);
    EXPECT_EQ(steps[1].buffer_stuffing_fix_wait, -1);
    EXPECT_EQ(steps[1].target_fps, 0);
    EXPECT_TRUE(steps[1].reset_pacing_at_start);
    EXPECT_TRUE(steps[1].external_boost_pulse);

    EXPECT_FLOAT_EQ(steps[2].duration_sec, 6.0f);
    EXPECT_EQ(steps[2].target_cpu_ms, 0);
    EXPECT_EQ(steps[2].target_gpu_ms, 8);
    EXPECT_EQ(steps[2].auto_pipeline, 2);
    EXPECT_EQ(steps[2].auto_swap_interval, -1);
    EXPECT_FALSE(steps[2].reset_pacing_at_start);
    EXPECT_FALSE(steps[2].external_boost_pulse);
}

TEST(ScenarioTest, ParseCustomStepsUnknownKeysAndInvalidInputs) {
    // Empty or null specifications yield a single 5 s idle step.
    for (const char* empty_spec : {static_cast<const char*>(nullptr), "", "   ;  |  "}) {
        const std::vector<ScenarioStep> steps = parse_custom_steps(empty_spec);
        ASSERT_EQ(steps.size(), 1u);
        EXPECT_FLOAT_EQ(steps[0].duration_sec, 5.0f);
        EXPECT_EQ(steps[0].target_cpu_ms, 0);
        EXPECT_EQ(steps[0].target_gpu_ms, 0);
    }

    // Unknown keys and invalid negative values are ignored without corrupting valid keys.
    const std::vector<ScenarioStep> steps =
            parse_custom_steps("2s:cpu=-5,gpu=-3,unknown_key=99,stuff_fix=-2,fps=0,cpu=9");
    ASSERT_EQ(steps.size(), 1u);
    EXPECT_FLOAT_EQ(steps[0].duration_sec, 2.0f);
    EXPECT_EQ(steps[0].target_cpu_ms, 9);
    EXPECT_EQ(steps[0].target_gpu_ms, 0);
    EXPECT_EQ(steps[0].buffer_stuffing_fix_wait, -1);
    EXPECT_EQ(steps[0].target_fps, 0);

    // Values exceeding the 64-byte v_clean buffer with trailing spaces/tabs truncate safely.
    const std::string overlong_spec = std::string("3s:cpu=7") + std::string(80, ' ') +
            "\t\t,unknown=" + std::string(80, 'x') + "   \t  ,gpu=11";
    const std::vector<ScenarioStep> overlong_steps = parse_custom_steps(overlong_spec.c_str());
    ASSERT_EQ(overlong_steps.size(), 1u);
    EXPECT_FLOAT_EQ(overlong_steps[0].duration_sec, 3.0f);
    EXPECT_EQ(overlong_steps[0].target_cpu_ms, 7);
    EXPECT_EQ(overlong_steps[0].target_gpu_ms, 11);

    // Step count is capped at kMaxScenarioSteps (32).
    std::string many_steps;
    for (int i = 0; i < 40; ++i) {
        if (i > 0) many_steps += ";";
        many_steps += "1s:cpu=5";
    }
    EXPECT_EQ(parse_custom_steps(many_steps.c_str()).size(),
              static_cast<size_t>(kMaxScenarioSteps));
}

TEST(ScenarioTest, RequiredCalibrationsForSteps) {
    const ScenarioDef* custom = find_scenario("CUSTOM", false);
    ASSERT_NE(custom, nullptr);

    // Zero-load steps require no calibrations.
    {
        const auto steps = build_scenario_steps(*custom, "5s:cpu=0,gpu=0");
        const ScenarioTraits traits = scenario_traits(*custom, steps);
        EXPECT_FALSE(traits.uses_cpu);
        EXPECT_FALSE(traits.uses_gpu);
        EXPECT_EQ(required_calibrations(traits, CALIBRATION_GLES), 0u);
        EXPECT_EQ(required_calibrations(traits, CALIBRATION_VULKAN), 0u);
    }

    // Boost pulses burn a fixed iteration count and require no calibrations.
    {
        const auto steps = build_scenario_steps(*custom, "5s:boost=on");
        const ScenarioTraits traits = scenario_traits(*custom, steps);
        EXPECT_FALSE(traits.uses_cpu);
        EXPECT_FALSE(traits.uses_gpu);
        EXPECT_TRUE(traits.needs_boost);
        EXPECT_EQ(required_calibrations(traits, CALIBRATION_GLES), 0u);
    }

    // CPU-only steps require only CALIBRATION_CPU.
    {
        const auto steps = build_scenario_steps(*custom, "5s:cpu=10,gpu=0");
        const ScenarioTraits traits = scenario_traits(*custom, steps);
        EXPECT_TRUE(traits.uses_cpu);
        EXPECT_FALSE(traits.uses_gpu);
        EXPECT_EQ(required_calibrations(traits, CALIBRATION_GLES),
                  calibration_bit(CALIBRATION_CPU));
        EXPECT_EQ(required_calibrations(traits, CALIBRATION_VULKAN),
                  calibration_bit(CALIBRATION_CPU));
    }

    // GPU-only steps require only the active render API's GPU calibration.
    {
        const auto steps = build_scenario_steps(*custom, "5s:cpu=0,gpu=12");
        const ScenarioTraits traits = scenario_traits(*custom, steps);
        EXPECT_FALSE(traits.uses_cpu);
        EXPECT_TRUE(traits.uses_gpu);
        EXPECT_EQ(required_calibrations(traits, CALIBRATION_GLES),
                  calibration_bit(CALIBRATION_GLES));
        EXPECT_EQ(required_calibrations(traits, CALIBRATION_VULKAN),
                  calibration_bit(CALIBRATION_VULKAN));
    }

    // Jitter-only steps (target_cpu_ms == 0, jitter_max_ms > 0) require CALIBRATION_CPU.
    {
        const auto steps = build_scenario_steps(*custom, "5s:jit=6-14");
        const ScenarioTraits traits = scenario_traits(*custom, steps);
        EXPECT_TRUE(traits.uses_cpu);
        EXPECT_FALSE(traits.uses_gpu);
        EXPECT_EQ(required_calibrations(traits, CALIBRATION_GLES),
                  calibration_bit(CALIBRATION_CPU));
    }

    // Combined CPU+GPU steps require both CPU and the render API's GPU calibration;
    // explicit fps <= 30 sets explicit_30fps.
    {
        const auto steps = build_scenario_steps(*custom, "3s:cpu=6,fps=30; 3s:gpu=10");
        const ScenarioTraits traits = scenario_traits(*custom, steps);
        EXPECT_TRUE(traits.uses_cpu);
        EXPECT_TRUE(traits.uses_gpu);
        EXPECT_TRUE(traits.explicit_30fps);
        EXPECT_EQ(required_calibrations(traits, CALIBRATION_GLES),
                  calibration_bit(CALIBRATION_CPU) | calibration_bit(CALIBRATION_GLES));
        EXPECT_EQ(required_calibrations(traits, CALIBRATION_VULKAN),
                  calibration_bit(CALIBRATION_CPU) | calibration_bit(CALIBRATION_VULKAN));
    }
}

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

// ============================================================================
// TelemetryRingBuffer histogram binning tests
// ============================================================================

TEST(TelemetryRingBufferTest, CaptureSnapshotBinsWorkByCombinedCpuAndGpuMs) {
    samples::TelemetryRingBuffer ring;
    const std::array<bool, samples::TelemetrySnapshot::kNumFpsGuidelines> supported = {false, true,
                                                                                       true, true,
                                                                                       true};

    // Frame 1: CPU-only (6.2 ms CPU, 0.0 ms GPU -> workMs = 6.2 -> bin 6).
    g_test_ui_snapshot = {};
    g_test_ui_snapshot.last_frame_expected_ms = 6.0f;
    g_test_ui_snapshot.last_frame_real_cpu_ms = 6.2f;
    ring.RecordFrame(16666667ULL, /*gpuWorkMs=*/0.0f, "classic", supported);

    // Frame 2: GPU-only (0.0 ms CPU, 11.4 ms GPU -> workMs = 11.4 -> bin 11).
    g_test_ui_snapshot.last_frame_expected_ms = 11.0f;
    g_test_ui_snapshot.last_frame_real_cpu_ms = 0.0f;
    ring.RecordFrame(16666667ULL, /*gpuWorkMs=*/11.4f, "classic", supported);

    // Frame 3: Combined CPU + GPU (4.3 ms CPU, 9.2 ms GPU -> workMs = 13.5 -> bin 13).
    g_test_ui_snapshot.last_frame_expected_ms = 13.0f;
    g_test_ui_snapshot.last_frame_real_cpu_ms = 4.3f;
    ring.RecordFrame(16666667ULL, /*gpuWorkMs=*/9.2f, "classic", supported);

    samples::TelemetrySnapshot snap{};
    ring.CaptureSnapshot(&snap);

    EXPECT_EQ(snap.histogram.windowFrames, 3);
    EXPECT_EQ(snap.histogram.workBins[6], 1);
    EXPECT_FLOAT_EQ(snap.histogram.workCpuSums[6], 6.2f);
    EXPECT_FLOAT_EQ(snap.histogram.workTotalSums[6], 6.2f);

    EXPECT_EQ(snap.histogram.workBins[11], 1);
    EXPECT_FLOAT_EQ(snap.histogram.workCpuSums[11], 0.0f);
    EXPECT_FLOAT_EQ(snap.histogram.workTotalSums[11], 11.4f);

    EXPECT_EQ(snap.histogram.workBins[13], 1);
    EXPECT_FLOAT_EQ(snap.histogram.workCpuSums[13], 4.3f);
    EXPECT_NEAR(snap.histogram.workTotalSums[13], 13.5f, 1e-5f);
    EXPECT_EQ(snap.histogram.workBins[4], 0);
}

} // namespace
} // namespace dvfs
