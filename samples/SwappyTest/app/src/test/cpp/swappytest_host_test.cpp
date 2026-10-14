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
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "CalibrationKind.h"
#include "DvfsBenchmark.h"
#include "Scenario.h"

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

} // namespace
} // namespace dvfs
