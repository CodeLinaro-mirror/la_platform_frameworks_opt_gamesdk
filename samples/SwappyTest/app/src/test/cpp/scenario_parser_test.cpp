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
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "Scenario.h"
#include "ScenarioBuilder.h"
#include "ScenarioCatalog.h"
#include "ScenarioParser.h"
#include "scenario_test_util.h"

namespace dvfs {
namespace {

using namespace std::chrono_literals;

Scenario parse_ok(const std::string& spec) {
    Scenario scenario;
    EXPECT_EQ(parse_custom_scenario(spec, &scenario), "") << spec;
    return scenario;
}

// Returns the failure of a spec that cannot run.
std::string parse_failure(const std::string& spec) {
    Scenario scenario;
    const std::string failure = parse_custom_scenario(spec, &scenario);
    EXPECT_EQ(failure.rfind("bad_custom_scenario:", 0), 0u) << spec << " -> " << failure;
    EXPECT_EQ(scenario.title(), "CUSTOM") << spec;
    EXPECT_EQ(scenario.description(), "CUSTOM (0 steps): " + spec);
    EXPECT_EQ(scenario.step_count(), 0) << spec;
    EXPECT_TRUE(scenario.ops().empty()) << spec;
    return failure;
}

void expect_same_steps(const Scenario& actual, const Scenario& expected) {
    EXPECT_EQ(actual.step_names(), expected.step_names());
    ASSERT_EQ(actual.ops().size(), expected.ops().size());
    for (size_t i = 0; i < actual.ops().size(); ++i) {
        EXPECT_EQ(actual.ops()[i].kind, expected.ops()[i].kind) << "op " << i;
        EXPECT_EQ(actual.ops()[i].operand, expected.ops()[i].operand) << "op " << i;
        EXPECT_EQ(actual.ops()[i].partner, expected.ops()[i].partner) << "op " << i;
    }
}

TEST(ScenarioParserTest, HeaderPreludeStepsAndSettings) {
    const std::string json = R"({
        "scenario": "X1",
        "description": "X1: a test",
        "swap_interval": "30fps",
        "cpu": 5,
        "gpu": 2,
        "steps": [
            {
                "name": "a",
                "auto_pipeline_mode": "off",
                "auto_swap_interval": false,
                "buffer_stuffing_fix_wait": 2,
                "frames": 3
            },
            {
                "name": "b",
                "duration": 2.0
            }
        ]
    })";

    const Scenario program = parse_ok(json);
    ScenarioBuilder expected;
    expected.set_swap_interval_ns(kSwapInterval30FpsNs).cpu(5ms).gpu(2ms);
    expected.step("a")
            .set_auto_pipeline_mode(ScenarioPipelineMode::Off)
            .set_auto_swap_interval(false)
            .set_buffer_stuffing_fix_wait(2)
            .frames(3);
    expected.step("b").frames_for(2s);

    expect_same_steps(program, built(expected));
    EXPECT_EQ(program.title(), "X1");
    EXPECT_EQ(program.description(), "X1: a test");
    EXPECT_TRUE(program.uses_cpu());
    EXPECT_TRUE(program.uses_gpu());
    EXPECT_FALSE(program.needs_boost());
}

TEST(ScenarioParserTest, SwappySettingValues) {
    {
        const std::string json = R"({
            "steps": [{
                "auto_pipeline_mode": "on",
                "auto_swap_interval": true,
                "buffer_stuffing_fix_wait": 0,
                "swap_interval": "60fps",
                "frames": 1
            }]
        })";
        const Scenario program = parse_ok(json);
        ScenarioBuilder expected;
        expected.set_auto_pipeline_mode(ScenarioPipelineMode::On)
                .set_auto_swap_interval(true)
                .set_buffer_stuffing_fix_wait(0)
                .set_swap_interval_ns(kSwapInterval60FpsNs)
                .frames(1);
        expect_same_steps(program, built(expected));
    }
    {
        const std::string json = R"({
            "steps": [{
                "auto_pipeline_mode": "auto",
                "auto_swap_interval": "off",
                "swap_interval": "16.666ms",
                "frames": 1
            }]
        })";
        const Scenario program = parse_ok(json);
        ScenarioBuilder expected;
        expected.set_auto_pipeline_mode(ScenarioPipelineMode::Auto)
                .set_auto_swap_interval(false)
                .set_swap_interval_ns(16666000)
                .frames(1);
        expect_same_steps(program, built(expected));
    }
    {
        const Scenario program = parse_ok(
                R"({"auto_pipeline_mode": true, "auto_swap_interval": "on", "frames": 1})");
        ScenarioBuilder expected;
        expected.set_auto_pipeline_mode(ScenarioPipelineMode::Auto)
                .set_auto_swap_interval(true)
                .frames(1);
        expect_same_steps(program, built(expected));
    }
    for (const char* json : {R"({"swap_interval": "16666666ns", "frames": 1})",
                             R"({"swap_interval": 16666666, "frames": 1})"}) {
        const Scenario program = parse_ok(json);
        ScenarioBuilder expected;
        expected.set_swap_interval_ns(16666666).frames(1);
        expect_same_steps(program, built(expected));
    }
}

TEST(ScenarioParserTest, BareStepsArrayAndSingleStepObject) {
    // Array root
    {
        const std::string json = R"([
            {"name": "step1", "cpu": 4, "frames": 10},
            {"name": "step2", "gpu": 6, "frames": 20}
        ])";
        const Scenario program = parse_ok(json);
        ScenarioBuilder expected;
        expected.step("step1").cpu(4ms).frames(10);
        expected.step("step2").gpu(6ms).frames(20);
        expect_same_steps(program, built(expected));
        EXPECT_EQ(program.step_count(), 2);
    }
    // Single step object root
    {
        const std::string json = R"({
            "cpu": 12,
            "gpu": 8,
            "duration": 3.0
        })";
        const Scenario program = parse_ok(json);
        ScenarioBuilder expected;
        expected.cpu(12ms).gpu(8ms).frames_for(3s);
        expect_same_steps(program, built(expected));
        EXPECT_EQ(program.step_count(), 1);
        EXPECT_TRUE(program.step_names().empty());
    }
    // Single step object root with name
    {
        const std::string json = R"({
            "name": "OnlyStep",
            "cpu": 10,
            "frames": 50
        })";
        const Scenario program = parse_ok(json);
        ScenarioBuilder expected;
        expected.step("OnlyStep").cpu(10ms).frames(50);
        expect_same_steps(program, built(expected));
        EXPECT_EQ(program.step_count(), 1);
        ASSERT_EQ(program.step_names().size(), 1u);
        EXPECT_EQ(program.step_names()[0], "1. OnlyStep");
    }
}

TEST(ScenarioParserTest, RepeatLoops) {
    // Repeat count with nested actions
    {
        const std::string json = R"({
            "steps": [{
                "name": "loop_count",
                "repeat": 3,
                "actions": [
                    {"cpu": 6, "frames": 1},
                    {"cpu": 7, "frames": 1}
                ]
            }]
        })";
        const Scenario program = parse_ok(json);
        ScenarioBuilder expected;
        expected.step("loop_count").repeat(3, [](ScenarioBuilder& b) {
            b.cpu(6ms).frames(1).cpu(7ms).frames(1);
        });
        expect_same_steps(program, built(expected));
    }
    // Repeat with a duration string, directly or in a repeat object
    for (const char* json : {
                 R"({"name": "timed", "repeat": "5s", "actions": [{"cpu": 10, "frames": 1}]})",
                 R"({"name": "timed", "repeat": {"duration": "5s", "actions": [{"cpu": 10, "frames": 1}]}})",
                 R"({"name": "timed", "repeat": {"duration": 5}, "actions": [{"cpu": 10, "frames": 1}]})",
         }) {
        const Scenario program = parse_ok(json);
        ScenarioBuilder expected;
        expected.step("timed").repeat_for(5s, [](ScenarioBuilder& b) { b.cpu(10ms).frames(1); });
        expect_same_steps(program, built(expected));
    }
    // Nested repeats
    {
        const std::string json = R"({
            "steps": [{
                "repeat": {
                    "count": 2,
                    "actions": [{
                        "repeat": {
                            "count": 3,
                            "actions": [{"frames": 1}]
                        }
                    }]
                }
            }]
        })";
        const Scenario program = parse_ok(json);
        ScenarioBuilder expected;
        expected.repeat(2, [](ScenarioBuilder& outer) {
            outer.repeat(3, [](ScenarioBuilder& inner) { inner.frames(1); });
        });
        expect_same_steps(program, built(expected));
    }
}

TEST(ScenarioParserTest, WorkloadFramesAndDurationFormats) {
    {
        const Scenario program = parse_ok(R"({"cpu_ms": 15, "gpu_ms": "25ms", "frames": "120"})");
        ScenarioBuilder expected;
        expected.cpu(15ms).gpu(25ms).frames(120);
        expect_same_steps(program, built(expected));
    }
    {
        const Scenario program = parse_ok(R"({"cpu": 0.5, "gpu": "500000ns", "frames": 1})");
        ScenarioBuilder expected;
        expected.cpu(500us).gpu(500us).frames(1);
        expect_same_steps(program, built(expected));
        EXPECT_TRUE(program.uses_cpu());
        EXPECT_TRUE(program.uses_gpu());
    }
    {
        const Scenario program = parse_ok(R"({"duration_s": 1.5})");
        ScenarioBuilder expected;
        expected.frames_for(1500ms);
        expect_same_steps(program, built(expected));
    }
    {
        const Scenario program = parse_ok(R"({"frames": "500ms"})");
        ScenarioBuilder expected;
        expected.frames_for(500ms);
        expect_same_steps(program, built(expected));
    }
    {
        const Scenario program = parse_ok(R"({"f": "2s"})");
        ScenarioBuilder expected;
        expected.frames_for(2s);
        expect_same_steps(program, built(expected));
    }
}

TEST(ScenarioParserTest, BoostAndResetPacing) {
    const std::string json = R"({
        "steps": [{
            "reset_pacing": true,
            "boost_ms": 500,
            "frames": 60
        }]
    })";
    const Scenario program = parse_ok(json);
    ScenarioBuilder expected;
    expected.reset_frame_pacing().boost(500ms).frames(60);
    expect_same_steps(program, built(expected));
    EXPECT_TRUE(program.needs_boost());
}

TEST(ScenarioParserTest, LineAndBlockComments) {
    const std::string json = R"(
        // The header
        {"scenario": "commented", /* inline */ "swap_interval": "60fps"}
        /* The first step,
           over two lines */
        {"name": "a", "cpu": 6, "frames": 1}
        {"name": "b", "cpu": 12, "frames": 1} // trailing
    )";
    const Scenario program = parse_ok(json);
    ScenarioBuilder expected;
    expected.set_swap_interval_ns(kSwapInterval60FpsNs);
    expected.step("a").cpu(6ms).frames(1);
    expected.step("b").cpu(12ms).frames(1);
    expect_same_steps(program, built(expected));
    EXPECT_EQ(program.title(), "commented");
}

TEST(ScenarioParserTest, JsonSyntaxErrorsReportTheLine) {
    for (const char* spec : {"{", "[}", "{\"steps\": [", "{\"cpu\": }", "bogus", "# comment"}) {
        EXPECT_EQ(parse_failure(spec).rfind("bad_custom_scenario:L1: ", 0), 0u) << spec;
    }
    const std::string failure = parse_failure("{\"cpu\": 1, \"frames\": 1}\n{\"cpu\": 2,\n");
    EXPECT_EQ(failure.rfind("bad_custom_scenario:L2: ", 0), 0u) << failure;
    EXPECT_EQ(parse_failure("{\"frames\": 1}; {\"frames\": 1}")
                      .rfind("bad_custom_scenario:L1: ", 0),
              0u);
}

TEST(ScenarioParserTest, ErrorsReportTheJsonPath) {
    const std::pair<const char*, const char*> cases[] = {
            {R"({"steps": [{"name": "empty_step"}]})",
             R"(bad_custom_scenario:steps[0]: step "1. empty_step" has no frames)"},
            {R"({"cpu": -5, "frames": 1})",
             "bad_custom_scenario:steps[0]: cpu must not be negative"},
            {R"([{"frames": 1}, {"gpu": -1, "frames": 1}])",
             "bad_custom_scenario:steps[1]: gpu must not be negative"},
            {R"({"swap_interval": "invalid", "frames": 1})",
             R"(bad_custom_scenario:steps[0]: bad swap_interval value "invalid")"},
            {R"({"auto_pipeline_mode": "turbo", "frames": 1})",
             "bad_custom_scenario:steps[0]: auto_pipeline_mode must be on, off or auto"},
            {R"({"buffer_stuffing_fix_wait": -2, "frames": 1})",
             "bad_custom_scenario:steps[0]: set_buffer_stuffing_fix_wait must not be negative"},
            {R"({"frames": 0})", "bad_custom_scenario:steps[0]: frame count must be at least 1"},
            {R"({"steps": [{"repeat": 2, "actions": [{"cpu": 5}]}]})",
             "bad_custom_scenario:steps[0]: repeat body has no frames"},
            {R"({"bogus_field": 123, "frames": 1})",
             "bad_custom_scenario:steps[0]: unknown field 'bogus_field'"},
            {R"({"title": "T", "bogus_field": 1})",
             "bad_custom_scenario:header: unknown field 'bogus_field'"},
            {R"({"steps": [{"frames": 1}]} {"frames": 1})",
             "bad_custom_scenario:header: steps cannot be followed by more objects"},
            {R"({"steps": [{"frames": 1}, {"repeat": {"count": 2, "actions": [
                 {"frames": 1}, {"repeat": 3, "actions": [{"cpu": 1, "bogus": 2}]}]}}]})",
             "bad_custom_scenario:steps[1].actions[1].actions[0]: unknown field 'bogus'"},
            {R"({"repeat": {"count": 2, "actions": [{"frames": 1}]}, "actions": [{"frames": 1}]})",
             "bad_custom_scenario:steps[0]: actions given twice"},
            {R"([{"frames": 1, "actions": [{"frames": 1}, {"cpu": -1}]}])",
             "bad_custom_scenario:steps[0].actions[1]: cpu must not be negative"},
            {R"({"frames": 1, "steps": [{"frames": 1}]})",
             "bad_custom_scenario:header: frames, duration, repeat, actions and name belong in a "
             "step"},
            {R"({"repeat": {"count": "5s", "actions": [{"frames": 1}]}})",
             "bad_custom_scenario:steps[0].repeat: count must be a number"},
            {R"({"cpu": 1e400, "frames": 1})", "bad_custom_scenario:steps[0]: bad cpu value null"},
            {R"({"duration": "1e12s"})",
             R"(bad_custom_scenario:steps[0]: bad duration value "1e12s")"},
            {R"({"buffer_stuffing_fix_wait": 1e12, "frames": 1})",
             "bad_custom_scenario:steps[0]: buffer_stuffing_fix_wait must be an integer"},
    };
    for (const auto& [spec, failure] : cases) {
        EXPECT_EQ(parse_failure(spec), failure) << spec;
    }
}

TEST(ScenarioParserTest, AliasAndItsFieldTogetherIsAnError) {
    EXPECT_EQ(parse_failure(R"({"cpu": 5, "cpu_ms": 6, "frames": 1})"),
              "bad_custom_scenario:steps[0]: 'cpu' and 'cpu_ms' are the same field");
    EXPECT_EQ(parse_failure(R"({"id": "A", "scenario": "B", "steps": []})"),
              "bad_custom_scenario:header: 'id' and 'scenario' are the same field");
}

TEST(ScenarioParserTest, FramesMustBeAWholeCount) {
    EXPECT_EQ(parse_failure(R"({"frames": 1.5})"),
              "bad_custom_scenario:steps[0]: frames must be a whole number of frames, use "
              "duration for seconds");
}

TEST(ScenarioParserTest, CustomSpecsWithoutSteps) {
    for (const char* spec : {"", "   \n"}) {
        Scenario custom;
        EXPECT_EQ(parse_custom_scenario(spec, &custom), "empty_custom_scenario") << spec;
        EXPECT_EQ(custom.title(), "CUSTOM") << spec;
        EXPECT_EQ(custom.description(), "CUSTOM (0 steps): <empty spec>") << spec;
        EXPECT_TRUE(custom.ops().empty()) << spec;
    }

    // An object of settings alone is a header without steps.
    for (const char* spec : {R"({"cpu": 5})", R"({"auto_pipeline_mode": "on"})", R"({"steps": []})",
                             R"({"title": "T", "swap_interval": "60fps"})"}) {
        Scenario header_only;
        EXPECT_EQ(parse_custom_scenario(spec, &header_only), "empty_custom_scenario") << spec;
        EXPECT_EQ(header_only.step_count(), 0) << spec;
    }
}

TEST(ScenarioParserTest, TitleAndDescriptionDefaults) {
    const std::string simple = R"({"steps": [{"cpu": 1, "frames": 1}]})";
    const Scenario defaults = parse_ok(simple);
    EXPECT_EQ(defaults.title(), "CUSTOM");
    EXPECT_EQ(defaults.description(), "CUSTOM (1 steps): " + simple);

    const Scenario described =
            parse_ok(R"({"description": "My Scenario", "steps": [{"frames": 1}]})");
    EXPECT_EQ(described.title(), "CUSTOM");
    EXPECT_EQ(described.description(), "My Scenario");

    const std::string titled = R"({"id": "MINE"} {"frames": 1} {"frames": 2})";
    const Scenario titled_program = parse_ok(titled);
    EXPECT_EQ(titled_program.title(), "MINE");
    EXPECT_EQ(titled_program.description(), "CUSTOM (2 steps): " + titled);
}

TEST(ScenarioParserTest, KotlinShorthandParses) {
    char spec[128];
    std::snprintf(spec, sizeof(spec),
                  "{\"steps\": [{\"cpu\": %d, \"gpu\": %d, \"duration\": %.3f}]}", 8, 0, 5.0f);
    const Scenario program = parse_ok(spec);
    ScenarioBuilder expected;
    expected.cpu(8ms).gpu(0ms).frames_for(5s);
    expect_same_steps(program, built(expected));
    EXPECT_TRUE(program.step_names().empty());
    EXPECT_EQ(program.step_count(), 1);
    EXPECT_TRUE(program.uses_cpu());
    EXPECT_FALSE(program.uses_gpu());

    const Scenario mixed =
            parse_ok("{\"steps\": [{\"cpu\": 4, \"gpu\": 12, \"duration\": 0.500}]}");
    ScenarioBuilder expected_mixed;
    expected_mixed.cpu(4ms).gpu(12ms).frames_for(500ms);
    expect_same_steps(mixed, built(expected_mixed));
    EXPECT_TRUE(mixed.uses_cpu());
    EXPECT_TRUE(mixed.uses_gpu());
}

TEST(ScenarioParserTest, UnnamedStepWithBareActions) {
    const std::string json = R"({
        "ops": [
            {"cpu": 6, "frames": 1},
            {"cpu": 12, "frames": 1}
        ]
    })";
    const Scenario program = parse_ok(json);
    ScenarioBuilder expected;
    expected.cpu(6ms).frames(1).cpu(12ms).frames(1);
    expect_same_steps(program, built(expected));
    EXPECT_EQ(program.step_count(), 1);
    EXPECT_TRUE(program.step_names().empty());
}

TEST(ScenarioParserTest, MultipleUnnamedSteps) {
    for (const char* json : {
                 R"([{"actions": [{"cpu": 5, "frames": 2}]}, {"actions": [{"cpu": 15, "frames": 3}]}])",
                 R"({"steps": [{"cpu": 5, "frames": 2}, {"cpu": 15, "frames": 3}]})",
                 R"({"cpu": 5, "frames": 2} {"cpu": 15, "frames": 3})",
         }) {
        const Scenario program = parse_ok(json);
        ScenarioBuilder expected;
        expected.cpu(5ms).frames(2);
        expected.step("").cpu(15ms).frames(3);
        expect_same_steps(program, built(expected));
        EXPECT_EQ(program.step_count(), 2) << json;
        EXPECT_EQ(program.step_names(), (std::vector<std::string>{"1.", "2."})) << json;
    }
}

TEST(ScenarioParserTest, AllAllowedFunctionsShowcase) {
    const std::string json = R"({
        "id": "ALL_FEATURES",
        "description": "Showcase of all custom scenario features",
        "auto_pipeline_mode": "off",
        "auto_swap_interval": false,
        "buffer_stuffing_fix_wait": 1,
        "swap_interval": "60fps",
        "cpu": 5,
        "gpu": 2,
        "steps": [
            {
                "name": "step_one",
                "auto_pipeline_mode": "on",
                "auto_swap_interval": true,
                "buffer_stuffing_fix_wait": 0,
                "swap_interval": "30fps",
                "cpu_ms": 10,
                "gpu_ms": 15,
                "reset_pacing": true,
                "boost": 250,
                "duration_s": 2.0
            },
            {
                "step": "step_two_repeats",
                "actions": [
                    {
                        "repeat": 3,
                        "actions": [
                            {"cpu": 6, "frames": 1},
                            {"cpu": 12, "frames": 1}
                        ]
                    },
                    {
                        "repeat": "2s",
                        "actions": [
                            {"gpu": 8, "f": 1}
                        ]
                    }
                ]
            }
        ]
    })";

    const Scenario program = parse_ok(json);
    ScenarioBuilder expected;
    expected.set_auto_pipeline_mode(ScenarioPipelineMode::Off)
            .set_auto_swap_interval(false)
            .set_buffer_stuffing_fix_wait(1)
            .set_swap_interval_ns(kSwapInterval60FpsNs)
            .cpu(5ms)
            .gpu(2ms);
    expected.step("step_one")
            .set_auto_pipeline_mode(ScenarioPipelineMode::On)
            .set_auto_swap_interval(true)
            .set_buffer_stuffing_fix_wait(0)
            .set_swap_interval_ns(kSwapInterval30FpsNs)
            .cpu(10ms)
            .gpu(15ms)
            .reset_frame_pacing()
            .boost(250ms)
            .frames_for(2s);
    expected.step("step_two_repeats")
            .repeat(3, [](ScenarioBuilder& b) { b.cpu(6ms).frames(1).cpu(12ms).frames(1); })
            .repeat_for(2s, [](ScenarioBuilder& b) { b.gpu(8ms).frames(1); });

    expect_same_steps(program, built(expected));
    EXPECT_EQ(program.step_count(), 2);
    EXPECT_EQ(program.title(), "ALL_FEATURES");
    EXPECT_EQ(program.description(), "Showcase of all custom scenario features");
    EXPECT_TRUE(program.needs_boost());
}

TEST(ScenarioParserTest, StreamWithHeaderAndSteps) {
    const std::string json = R"(
        {"scenario": "mixed_stream", "swap_interval": "60fps"}
        {"name": "step1", "cpu": 10, "frames": 5}
        {
          "name": "step2",
          "repeat": {
            "count": 2,
            "actions": [{"cpu": 20, "frames": 1}]
          }
        }
        {"cpu": 5, "frames": 10}
    )";
    const Scenario program = parse_ok(json);
    ScenarioBuilder expected;
    expected.set_swap_interval_ns(kSwapInterval60FpsNs);
    expected.step("step1").cpu(10ms).frames(5);
    expected.step("step2").repeat(2, [](ScenarioBuilder& b) { b.cpu(20ms).frames(1); });
    expected.step("").cpu(5ms).frames(10);

    expect_same_steps(program, built(expected));
    EXPECT_EQ(program.title(), "mixed_stream");
    EXPECT_EQ(program.step_names(), (std::vector<std::string>{"1. step1", "2. step2", "3."}));
}

} // namespace
} // namespace dvfs
