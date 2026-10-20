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

// Tests ScenarioCatalog indexing, default "S1", unique "<title>: " descriptions, and
// exact/case-insensitive find_scenario lookups; plays every built-in scenario in scenarios/ on a
// fake clock to verify step counts/numbering and that CPU/GPU/boost traits match emitted frames.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "Scenario.h"
#include "ScenarioCatalog.h"
#include "ScenarioEngine.h"
#include "scenario_test_util.h"

namespace dvfs {
namespace {

TEST(ScenarioCatalogTest, CatalogScenariosLookupAndMetadata) {
    ASSERT_GT(ScenarioCatalog::scenario_count(), 0);
    EXPECT_EQ(ScenarioCatalog::scenario_at(0).title(), "S1");

    std::set<std::string> titles;
    for (int i = 0; i < ScenarioCatalog::scenario_count(); ++i) {
        const Scenario& scenario = ScenarioCatalog::scenario_at(i);
        const std::string& title = scenario.title();
        ASSERT_FALSE(title.empty()) << "Scenario at index " << i;
        EXPECT_TRUE(titles.insert(title).second) << "Duplicate scenario title " << title;

        const std::string prefix = title + ": ";
        EXPECT_EQ(scenario.description().rfind(prefix, 0), 0u) << title;
        EXPECT_GT(scenario.description().size(), prefix.size()) << title;

        EXPECT_EQ(ScenarioCatalog::find_scenario(title.c_str(), /*ignore_case=*/false), &scenario);

        std::string lower_title = title;
        for (char& c : lower_title) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        EXPECT_EQ(ScenarioCatalog::find_scenario(lower_title.c_str(), /*ignore_case=*/true),
                  &scenario);
        EXPECT_EQ(ScenarioCatalog::find_scenario(lower_title.c_str(), /*ignore_case=*/false),
                  nullptr);
    }
    EXPECT_EQ(ScenarioCatalog::find_scenario(nullptr, /*ignore_case=*/true), nullptr);
    EXPECT_EQ(ScenarioCatalog::find_scenario("S99", /*ignore_case=*/true), nullptr);
}

TEST(ScenarioCatalogTest, EveryScenarioPlaysStepsAndMatchesTraits) {
    for (int i = 0; i < ScenarioCatalog::scenario_count(); ++i) {
        const Scenario& scenario = ScenarioCatalog::scenario_at(i);
        SCOPED_TRACE(scenario.title());

        // Single-step scenarios have no step names; multi-step scenarios have
        // unique numbered step names ("1. ...", "2. ...", ...).
        const int step_count = scenario.step_count();
        ASSERT_GE(step_count, 1);
        EXPECT_LE(step_count, kMaxScenarioSteps);
        if (step_count == 1) {
            EXPECT_TRUE(scenario.step_names().empty());
        } else {
            ASSERT_EQ(static_cast<int>(scenario.step_names().size()), step_count);
            std::set<std::string> names;
            for (size_t step = 0; step < scenario.step_names().size(); ++step) {
                const std::string number = std::to_string(step + 1) + ". ";
                const std::string& step_name = scenario.step_names()[step];
                EXPECT_EQ(step_name.rfind(number, 0), 0u);
                EXPECT_GT(step_name.size(), number.size());
                EXPECT_TRUE(names.insert(step_name).second) << "Duplicate step name " << step_name;
            }
        }

        const std::vector<ScenarioEngine::Frame> frames = play(scenario);
        ASSERT_FALSE(frames.empty());
        const std::vector<size_t> starts = step_starts(frames);
        ASSERT_EQ(static_cast<int>(starts.size()), step_count);
        for (size_t step = 0; step < starts.size(); ++step) {
            EXPECT_EQ(frames[starts[step]].step_index, static_cast<int>(step));
        }

        const bool played_cpu =
                std::any_of(frames.begin(), frames.end(),
                            [](const ScenarioEngine::Frame& f) { return f.cpu_ms > 0.0; });
        const bool played_gpu =
                std::any_of(frames.begin(), frames.end(),
                            [](const ScenarioEngine::Frame& f) { return f.gpu_ms > 0.0; });
        const bool played_boost =
                std::any_of(frames.begin(), frames.end(),
                            [](const ScenarioEngine::Frame& f) { return f.boost_ms > 0; });
        EXPECT_TRUE(scenario.uses_cpu());
        EXPECT_EQ(scenario.uses_cpu(), played_cpu);
        EXPECT_EQ(scenario.uses_gpu(), played_gpu);
        EXPECT_EQ(scenario.needs_boost(), played_boost);
    }
}

} // namespace
} // namespace dvfs
