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
#include <stdlib.h>

extern "C" void Swappy_flushCoverage(void) __attribute__((weak));
extern "C" void __gcov_dump(void) __attribute__((weak));
extern "C" void __gcov_reset(void) __attribute__((weak));
extern "C" void __gcov_flush(void) __attribute__((weak));

namespace swappy {
namespace testing {

#ifndef AGDK_GCOV_PREFIX
#define AGDK_GCOV_PREFIX "/data/data/com.swappy.testapp/files"
#endif

// App-writable directory on Android where runtime .gcda files are written.
constexpr const char* kDefaultAndroidGcovPrefix = AGDK_GCOV_PREFIX;
// Strips leading host build-directory path components (up to 20 levels) so
// .gcda files are emitted inside GCOV_PREFIX.
constexpr const char* kDefaultGcovPrefixStrip = "20";

class GcovFlushEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        if (!Swappy_flushCoverage && !__gcov_dump && !__gcov_flush) {
            return;
        }
#ifdef __ANDROID__
        setenv("GCOV_PREFIX", kDefaultAndroidGcovPrefix, /*overwrite=*/0);
        setenv("GCOV_PREFIX_STRIP", kDefaultGcovPrefixStrip, /*overwrite=*/0);
#endif
    }

    void TearDown() override {
        if (!Swappy_flushCoverage && !__gcov_dump && !__gcov_flush) {
            return;
        }
        SetUp();
        if (Swappy_flushCoverage) {
            Swappy_flushCoverage();
        }
        if (__gcov_dump) {
            __gcov_dump();
            if (__gcov_reset) {
                __gcov_reset();
            }
        } else if (__gcov_flush) {
            __gcov_flush();
        }
    }
};

// C++17 inline variable guarantees a single registration across all translation
// units linked into the same test binary or shared library.
[[maybe_unused]] inline const ::testing::Environment* const kGcovFlushEnvironment =
        ::testing::AddGlobalTestEnvironment(new GcovFlushEnvironment());

}  // namespace testing
}  // namespace swappy
