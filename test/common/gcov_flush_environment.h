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

#include <gtest/gtest.h>

#include "gcov_flush.h"

namespace agdk {
namespace testing {

// Flushes gcov counters after the whole suite ran, before `am instrument`
// kills the process.
class GcovFlushEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        if (HasGcovRuntime()) {
            SetGcovPrefix();
        }
    }

    void TearDown() override {
        FlushGcovCounters();
    }
};

// C++17 inline variable guarantees a single registration across all
// translation units linked into the same test binary or shared library.
[[maybe_unused]] inline const ::testing::Environment* const kGcovFlushEnvironment =
        ::testing::AddGlobalTestEnvironment(new GcovFlushEnvironment());

} // namespace testing
} // namespace agdk
