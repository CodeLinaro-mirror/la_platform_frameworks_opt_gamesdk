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

#include <chrono>
#include <string>
#include <vector>

#include "Scenario.h"
#include "ScenarioBuilder.h"
#include "ScenarioEngine.h"
#include "scenario_test_util.h"

namespace dvfs {
namespace {

using namespace std::chrono_literals;

// Finishes builder, which must not hold a valid scenario, and returns its error.
std::string build_error(ScenarioBuilder& builder) {
    EXPECT_FALSE(builder.finish());
    return builder.error();
}

TEST(ScenarioBuilderTest, TitlePreludeStepsAndSettings) {
    ScenarioBuilder builder("X1", "X1: a test");
    builder.set_swap_interval_ns(kSwapInterval30FpsNs).cpu(5ms).gpu(2ms);
    builder.step("a")
            .set_auto_pipeline_mode(ScenarioPipelineMode::Off)
            .set_auto_swap_interval(false)
            .set_buffer_stuffing_fix_wait(2)
            .frames(3);
    builder.step("b").frames_for(2s);
    const Scenario scenario = built(builder);
    EXPECT_EQ(scenario.title(), "X1");
    EXPECT_EQ(scenario.description(), "X1: a test");
    EXPECT_TRUE(scenario.uses_cpu());
    EXPECT_TRUE(scenario.uses_gpu());
    EXPECT_FALSE(scenario.needs_boost());

    ASSERT_EQ(scenario.step_names().size(), 2u);
    EXPECT_EQ(scenario.step_names()[0], "1. a");
    EXPECT_EQ(scenario.step_names()[1], "2. b");

    const std::vector<ScenarioEngine::Frame> frames = play(scenario);
    ASSERT_EQ(frames.size(), 3u + 200u);
    EXPECT_EQ(step_starts(frames), (std::vector<size_t>{0, 3}));
    // The prelude's swap interval and step 0's settings fire on the first frame only.
    EXPECT_EQ(frames[0].swap_interval_ns, kSwapInterval30FpsNs);
    EXPECT_EQ(frames[0].auto_pipeline_mode, ScenarioPipelineMode::Off);
    EXPECT_EQ(frames[0].auto_swap_interval, false);
    EXPECT_EQ(frames[0].buffer_stuffing_fix_wait, 2);
    for (size_t i = 1; i < frames.size(); ++i) {
        EXPECT_FALSE(frames[i].swap_interval_ns.has_value()) << i;
        EXPECT_FALSE(frames[i].auto_pipeline_mode.has_value()) << i;
        EXPECT_FALSE(frames[i].auto_swap_interval.has_value()) << i;
        EXPECT_FALSE(frames[i].buffer_stuffing_fix_wait.has_value()) << i;
    }
    for (const ScenarioEngine::Frame& frame : frames) {
        EXPECT_EQ(frame.cpu_ms, 5);
        EXPECT_EQ(frame.gpu_ms, 2);
    }
    EXPECT_EQ(frames[2].step_index, 0);
    EXPECT_EQ(frames[3].step_index, 1);
}

TEST(ScenarioBuilderTest, SwappySettingValues) {
    ScenarioBuilder on_builder;
    on_builder.set_auto_pipeline_mode(ScenarioPipelineMode::On)
            .set_auto_swap_interval(true)
            .set_buffer_stuffing_fix_wait(0)
            .set_swap_interval_ns(kSwapInterval60FpsNs)
            .frames();
    const Scenario on = built(on_builder);
    EXPECT_TRUE(on.step_names().empty());
    EXPECT_EQ(on.step_count(), 1);
    const std::vector<ScenarioEngine::Frame> on_frames = play(on);
    ASSERT_EQ(on_frames.size(), 1u);
    EXPECT_EQ(on_frames[0].auto_pipeline_mode, ScenarioPipelineMode::On);
    EXPECT_EQ(on_frames[0].auto_swap_interval, true);
    EXPECT_EQ(on_frames[0].buffer_stuffing_fix_wait, 0);
    EXPECT_EQ(on_frames[0].swap_interval_ns, kSwapInterval60FpsNs);

    ScenarioBuilder automatic_builder;
    automatic_builder.set_auto_pipeline_mode(ScenarioPipelineMode::Auto)
            .set_swap_interval_ns(kSwapInterval30FpsNs)
            .frames();
    const Scenario automatic = built(automatic_builder);
    EXPECT_TRUE(automatic.step_names().empty());
    EXPECT_EQ(automatic.step_count(), 1);
    const std::vector<ScenarioEngine::Frame> auto_frames = play(automatic);
    ASSERT_EQ(auto_frames.size(), 1u);
    EXPECT_EQ(auto_frames[0].auto_pipeline_mode, ScenarioPipelineMode::Auto);
    EXPECT_EQ(auto_frames[0].swap_interval_ns, kSwapInterval30FpsNs);

    // Settings before the first frame of a later step fire on that step's first frame.
    ScenarioBuilder later_builder;
    later_builder.step("a").frames();
    later_builder.step("b").set_swap_interval_ns(kSwapInterval30FpsNs).frames();
    const Scenario later = built(later_builder);
    ASSERT_EQ(later.step_names().size(), 2u);
    const std::vector<ScenarioEngine::Frame> later_frames = play(later);
    ASSERT_EQ(later_frames.size(), 2u);
    EXPECT_FALSE(later_frames[0].swap_interval_ns.has_value());
    EXPECT_EQ(later_frames[1].swap_interval_ns, kSwapInterval30FpsNs);
}

TEST(ScenarioBuilderTest, UnsetSettingsRemainEmpty) {
    // A step without Swappy settings changes nothing.
    ScenarioBuilder plain_builder;
    plain_builder.step("a").cpu(5ms).frames();
    const Scenario plain = built(plain_builder);
    ASSERT_EQ(plain.step_names().size(), 1u);
    const std::vector<ScenarioEngine::Frame> plain_frames = play(plain);
    ASSERT_EQ(plain_frames.size(), 1u);
    EXPECT_FALSE(plain_frames[0].auto_pipeline_mode.has_value());
    EXPECT_FALSE(plain_frames[0].auto_swap_interval.has_value());
    EXPECT_FALSE(plain_frames[0].buffer_stuffing_fix_wait.has_value());
    EXPECT_FALSE(plain_frames[0].swap_interval_ns.has_value());

    // A prelude set_swap_interval_ns fires on step 0's first frame, and a later
    // change on step 1's first frame.
    ScenarioBuilder prelude_builder;
    prelude_builder.set_swap_interval_ns(kSwapInterval30FpsNs);
    prelude_builder.step("a").frames();
    prelude_builder.step("b").set_auto_pipeline_mode(ScenarioPipelineMode::Off).frames();
    const Scenario prelude = built(prelude_builder);
    ASSERT_EQ(prelude.step_names().size(), 2u);
    const std::vector<ScenarioEngine::Frame> prelude_frames = play(prelude);
    ASSERT_EQ(prelude_frames.size(), 2u);
    EXPECT_EQ(prelude_frames[0].swap_interval_ns, kSwapInterval30FpsNs);
    EXPECT_FALSE(prelude_frames[0].auto_pipeline_mode.has_value());
    EXPECT_FALSE(prelude_frames[1].swap_interval_ns.has_value());
    EXPECT_EQ(prelude_frames[1].auto_pipeline_mode, ScenarioPipelineMode::Off);
}

TEST(ScenarioBuilderTest, SettersPersistAcrossSteps) {
    ScenarioBuilder builder;
    builder.step("a").cpu(10ms).gpu(3ms).frames();
    builder.step("b").frames();
    builder.step("c").cpu(500us).gpu(1500us).frames();
    builder.step("d").cpu(0ms).frames();
    const Scenario scenario = built(builder);
    EXPECT_EQ(cpu_sequence(scenario), (std::vector<double>{10, 10, 0.5, 0}));
    EXPECT_EQ(gpu_sequence(scenario), (std::vector<double>{3, 3, 1.5, 1.5}));
    EXPECT_EQ(step_starts(play(scenario)), (std::vector<size_t>{0, 1, 2, 3}));
}

TEST(ScenarioBuilderTest, FramesOutsideAStepOpenAnUnnamedStep) {
    const Scenario scenario = built(ScenarioBuilder().cpu(0ms).gpu(0ms).frames_for(5s));
    EXPECT_TRUE(scenario.step_names().empty());
    EXPECT_EQ(scenario.step_count(), 1);
    EXPECT_FALSE(scenario.uses_cpu());
    EXPECT_FALSE(scenario.uses_gpu());
    const std::vector<ScenarioEngine::Frame> frames = play(scenario);
    ASSERT_EQ(frames.size(), 500u);
    EXPECT_EQ(frames[0].step_index, 0);

    // The unnamed step lasts up to the next named one.
    ScenarioBuilder builder;
    builder.cpu(1ms).frames(2).cpu(2ms).frames();
    builder.step("named").cpu(3ms).frames();
    const Scenario two_steps = built(builder);
    ASSERT_EQ(two_steps.step_names().size(), 2u);
    EXPECT_EQ(two_steps.step_names()[0], "1.");
    EXPECT_EQ(two_steps.step_names()[1], "2. named");
    EXPECT_EQ(step_starts(play(two_steps)), (std::vector<size_t>{0, 3}));
}

TEST(ScenarioBuilderTest, EmptyBuilderBuildsNoSteps) {
    ScenarioBuilder builder;
    const Scenario scenario = built(builder);
    EXPECT_TRUE(scenario.step_names().empty());
    EXPECT_EQ(scenario.step_count(), 0);
    EXPECT_TRUE(scenario.ops().empty());
    EXPECT_TRUE(play(scenario).empty());
}

TEST(ScenarioBuilderTest, RejectsZeroAndNegativeValues) {
    EXPECT_EQ(build_error(ScenarioBuilder().frames(0)), "frame count must be at least 1");
    EXPECT_EQ(build_error(ScenarioBuilder().frames(-2)), "frame count must be at least 1");
    EXPECT_EQ(build_error(ScenarioBuilder().frames_for(0s)), "frame duration must be above 0 s");
    EXPECT_EQ(build_error(ScenarioBuilder().repeat(0)), "repeat count must be at least 1");
    EXPECT_EQ(build_error(ScenarioBuilder().repeat_for(-1s)), "repeat duration must be above 0 s");
    EXPECT_EQ(build_error(ScenarioBuilder().cpu(-1ns).frames()), "cpu must not be negative");
    EXPECT_EQ(build_error(ScenarioBuilder().gpu(-1ns).frames()), "gpu must not be negative");
    EXPECT_EQ(build_error(ScenarioBuilder().boost(0ms).frames()),
              "boost duration must be above 0 ms");
    EXPECT_EQ(build_error(ScenarioBuilder().boost(-1ms).frames()),
              "boost duration must be above 0 ms");
    EXPECT_EQ(build_error(ScenarioBuilder().set_swap_interval_ns(0).frames()),
              "set_swap_interval_ns must be at least 1 ns");
    EXPECT_EQ(build_error(ScenarioBuilder().set_buffer_stuffing_fix_wait(-1).frames()),
              "set_buffer_stuffing_fix_wait must not be negative");
    EXPECT_EQ(play(ScenarioBuilder().frames(1).frames_for(10ms)).size(), 2u);
}

TEST(ScenarioBuilderTest, RejectsMisplacedCommands) {
    EXPECT_EQ(build_error(ScenarioBuilder().repeat(2).step("a")),
              "step inside repeat, missing end");
    EXPECT_EQ(build_error(ScenarioBuilder().frames().end_repeat()), "unmatched end");
    EXPECT_EQ(build_error(ScenarioBuilder().step("a").repeat(2).frames()),
              "missing end for repeat");
    EXPECT_EQ(build_error(ScenarioBuilder()
                                  .cpu(5ms)
                                  .frames()
                                  .set_swap_interval_ns(kSwapInterval30FpsNs)
                                  .frames()),
              "set_swap_interval_ns must come before the first frame of the step and outside "
              "repeat blocks");

    // Later iterations of a repeat run after the step's first frame, so
    // Swappy settings stay outside repeat blocks; cpu / gpu and actions may
    // be in.
    ScenarioBuilder in_repeat;
    in_repeat.step("a").repeat(2).set_auto_pipeline_mode(ScenarioPipelineMode::Off);
    EXPECT_EQ(build_error(in_repeat),
              "set_auto_pipeline_mode must come before the first frame of the step and outside "
              "repeat blocks");
    EXPECT_EQ(cpu_sequence(ScenarioBuilder().repeat(2).cpu(3ms).frames().boost(500ms).end_repeat()),
              (std::vector<double>{3, 3}));
}

TEST(ScenarioBuilderTest, RejectsStepsAndRepeatBodiesWithoutFrames) {
    // A step that never plays a frame never starts, so its settings and
    // actions would be lost.
    ScenarioBuilder empty_step;
    empty_step.cpu(5ms).frames();
    empty_step.step("empty").set_auto_pipeline_mode(ScenarioPipelineMode::Off);
    EXPECT_EQ(build_error(empty_step), "step \"2. empty\" has no frames");
    EXPECT_EQ(build_error(ScenarioBuilder().step("a").reset_frame_pacing()),
              "step \"1. a\" has no frames");
    EXPECT_EQ(build_error(ScenarioBuilder().step("a").step("b").frames()),
              "step \"1. a\" has no frames");

    EXPECT_EQ(build_error(ScenarioBuilder().repeat(3).cpu(5ms).end_repeat()),
              "repeat body has no frames");
    EXPECT_EQ(build_error(ScenarioBuilder().repeat(2).repeat(3).cpu(6ms).end_repeat()),
              "repeat body has no frames");

    // A nested block's frames count for the blocks around it.
    EXPECT_EQ(play(ScenarioBuilder()
                           .repeat(2)
                           .repeat(3)
                           .cpu(1ms)
                           .frames()
                           .end_repeat()
                           .end_repeat())
                      .size(),
              6u);
}

TEST(ScenarioBuilderTest, IgnoresCommandsAfterTheFirstError) {
    ScenarioBuilder builder;
    builder.frames(0).cpu(-1ms).step("a").frames();
    EXPECT_FALSE(builder.ok());
    EXPECT_EQ(builder.error(), "frame count must be at least 1");
    EXPECT_FALSE(builder.finish());
    EXPECT_EQ(builder.error(), "frame count must be at least 1");
}

TEST(ScenarioBuilderTest, LoadTraitsFollowThePlayedFrames) {
    // A load counts once a frame plays it, not when it is set.
    EXPECT_TRUE(built(ScenarioBuilder().cpu(5ms).frames()).uses_cpu());
    EXPECT_TRUE(built(ScenarioBuilder().gpu(5ms).step("a").frames()).uses_gpu());
    EXPECT_FALSE(built(ScenarioBuilder().cpu(5ms).cpu(0ms).frames()).uses_cpu());
    EXPECT_FALSE(built(ScenarioBuilder().step("z").frames().cpu(5ms)).uses_cpu());

    // A load set inside a repeat body plays in the body's next iteration,
    // also from a nested block, so it counts even after the body's frames.
    EXPECT_TRUE(built(ScenarioBuilder().repeat(2).frames().cpu(5ms).end_repeat()).uses_cpu());
    EXPECT_TRUE(built(ScenarioBuilder().repeat_for(2s).frames().gpu(5ms).end_repeat()).uses_gpu());
    EXPECT_TRUE(
            built(ScenarioBuilder().repeat(2).repeat(3).frames().end_repeat().cpu(5ms).end_repeat())
                    .uses_cpu());
    EXPECT_FALSE(built(ScenarioBuilder().repeat(2).frames().cpu(5ms).end_repeat()).uses_gpu());
    EXPECT_FALSE(
            built(ScenarioBuilder().cpu(5ms).repeat(2).cpu(0ms).frames().end_repeat()).uses_cpu());
    EXPECT_FALSE(
            built(ScenarioBuilder().repeat(2).cpu(0ms).frames().end_repeat().cpu(5ms)).uses_cpu());
}

TEST(ScenarioBuilderTest, EnforcesTheStepLimitAndAllowsDeepRepeatNesting) {
    ScenarioBuilder steps;
    for (int i = 0; i < kMaxScenarioSteps; ++i) steps.step("s").frames();
    EXPECT_TRUE(steps.ok());
    steps.step("one too many").frames();
    EXPECT_EQ(build_error(steps), "more than 32 steps");

    ScenarioBuilder deep;
    for (int i = 0; i < 16; ++i) deep.repeat(1);
    deep.frames();
    for (int i = 0; i < 16; ++i) deep.end_repeat();
    EXPECT_EQ(play(deep).size(), 1u);
}

TEST(ScenarioBuilderTest, RepeatBodyLambdaMatchesExplicitEnd) {
    ScenarioBuilder with_lambda;
    with_lambda.repeat(2, [](ScenarioBuilder& body) {
        body.cpu(1ms).frames();
        body.repeat_for(30ms, [](ScenarioBuilder& inner) { inner.cpu(2ms).frames(); });
    });
    ScenarioBuilder with_end;
    with_end.repeat(2)
            .cpu(1ms)
            .frames()
            .repeat_for(30ms)
            .cpu(2ms)
            .frames()
            .end_repeat()
            .end_repeat();
    const Scenario lambda_scenario = built(with_lambda);
    const Scenario end_scenario = built(with_end);
    ASSERT_EQ(lambda_scenario.ops().size(), end_scenario.ops().size());
    for (size_t i = 0; i < lambda_scenario.ops().size(); ++i) {
        EXPECT_EQ(lambda_scenario.ops()[i].kind, end_scenario.ops()[i].kind) << i;
        EXPECT_EQ(lambda_scenario.ops()[i].operand, end_scenario.ops()[i].operand) << i;
        EXPECT_EQ(lambda_scenario.ops()[i].partner, end_scenario.ops()[i].partner) << i;
    }
    EXPECT_EQ(cpu_sequence(lambda_scenario), (std::vector<double>{1, 2, 2, 2, 1, 2, 2, 2}));
}

} // namespace
} // namespace dvfs
