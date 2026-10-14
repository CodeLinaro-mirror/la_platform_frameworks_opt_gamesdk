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

// The built-in scenarios, one per file, each built by its own function.
// ScenarioCatalog.cpp lists them in UI order.

#include "Scenario.h"

namespace dvfs {

Scenario build_scenario_s1();
Scenario build_scenario_s2();
Scenario build_scenario_s3();
Scenario build_scenario_s4();
Scenario build_scenario_s5();
Scenario build_scenario_s7();
Scenario build_scenario_s10();
Scenario build_scenario_s11();
Scenario build_scenario_h1();
Scenario build_scenario_h2();
Scenario build_scenario_h3();
Scenario build_scenario_h4();
Scenario build_scenario_h6();
Scenario build_scenario_h7();
Scenario build_scenario_h8();
Scenario build_scenario_h9();
Scenario build_scenario_h10();

} // namespace dvfs
