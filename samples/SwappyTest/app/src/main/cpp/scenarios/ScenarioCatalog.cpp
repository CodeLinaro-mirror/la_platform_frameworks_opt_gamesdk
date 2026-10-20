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

#include "ScenarioCatalog.h"

#include <string.h>
#include <strings.h>

#include <vector>

#include "Scenario.h"
#include "Scenarios.h"

namespace dvfs {

namespace {

std::vector<Scenario> build_catalog() {
    // The built-in scenarios, in UI order. S1 is the default. S3 and H3, and
    // S5 and H7, are the same scenario under two ids; S3 is kept as an
    // on-demand alias and the default matrix runs H3.
    Scenario (*const kBuildScenarios[])() = {
            build_scenario_s1,  build_scenario_s2, build_scenario_s3,  build_scenario_s4,
            build_scenario_s5,  build_scenario_s7, build_scenario_s10, build_scenario_s11,
            build_scenario_h1,  build_scenario_h2, build_scenario_h3,  build_scenario_h4,
            build_scenario_h6,  build_scenario_h7, build_scenario_h8,  build_scenario_h9,
            build_scenario_h10,
    };
    std::vector<Scenario> catalog;
    for (Scenario (*build_scenario)() : kBuildScenarios) catalog.push_back(build_scenario());
    return catalog;
}

const std::vector<Scenario>& catalog() {
    static const std::vector<Scenario> kCatalog = build_catalog();
    return kCatalog;
}

} // namespace

const Scenario* ScenarioCatalog::find_scenario(const char* id, bool ignore_case) {
    if (!id) return nullptr;
    for (const Scenario& scenario : catalog()) {
        const char* scenario_id = scenario.title().c_str();
        if ((ignore_case ? strcasecmp(id, scenario_id) : strcmp(id, scenario_id)) == 0) {
            return &scenario;
        }
    }
    return nullptr;
}

int ScenarioCatalog::scenario_count() {
    return static_cast<int>(catalog().size());
}

const Scenario& ScenarioCatalog::scenario_at(int index) {
    return catalog()[index];
}

} // namespace dvfs
