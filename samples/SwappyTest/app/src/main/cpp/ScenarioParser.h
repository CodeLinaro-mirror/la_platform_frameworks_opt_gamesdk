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

#include <string>

#include "Scenario.h"

namespace dvfs {

// Parses the JSON spec of a CUSTOM run (see CUSTOM.md) into *scenario. Its
// title defaults to "CUSTOM" and its description to
// "CUSTOM (<n> steps): <spec>", n the number of steps in the spec.
// Returns "" when *scenario has at least one step, "empty_custom_scenario"
// when it has none, or "bad_custom_scenario:<where>: <message>" when the spec
// is invalid; <where> is "L<line>" for a JSON syntax error and a JSON path
// such as "header" or "steps[2].actions[1]" otherwise. After
// "bad_custom_scenario:..." *scenario is a scenario without steps titled
// "CUSTOM".
std::string parse_custom_scenario(const std::string& spec, Scenario* scenario);

} // namespace dvfs
