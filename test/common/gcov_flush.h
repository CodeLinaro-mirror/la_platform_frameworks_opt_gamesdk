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

#include <stdlib.h>

#include <fstream>
#include <string>

// Flush topology: each shared object that builds with --coverage has its own
// copy of the gcov runtime. That copy knows only the counters of the objects
// in the same shared object. Thus a call to the runtime from a test library
// flushes the test library and the static libraries that it links. An
// instrumented library that the test process loads separately must export
// its own flush entry point. Swappy_flushCoverage is that entry point for
// libswappy.so.
//
// LLVM 14+ removed __gcov_flush in favor of __gcov_dump + __gcov_reset. All
// symbols are weak so this header also compiles into builds without
// --coverage, where every call below is a no-op.
extern "C" void Swappy_flushCoverage(void) __attribute__((weak));
extern "C" void __gcov_dump(void) __attribute__((weak));
extern "C" void __gcov_reset(void) __attribute__((weak));
extern "C" void __gcov_flush(void) __attribute__((weak));

namespace agdk {
namespace testing {

// Strips leading host build-directory path components (up to 20 levels), so
// that the gcov runtime writes .gcda files inside GCOV_PREFIX.
constexpr const char* kGcovPrefixStrip = "20";

inline bool HasGcovRuntime() {
    return Swappy_flushCoverage || __gcov_dump || __gcov_flush;
}

// Returns the files directory of the app that runs the tests, or an empty
// string outside an app process. Android gives an app process the name of
// its package, and /proc/self/cmdline starts with that name. A package name
// always contains a dot and never a slash. A binary that starts from a shell
// has its path or file name there instead.
inline std::string AppFilesDir() {
    std::string process_name;
    std::ifstream cmdline("/proc/self/cmdline");
    std::getline(cmdline, process_name, '\0');
    if (process_name.find('.') == std::string::npos ||
        process_name.find('/') != std::string::npos) {
        return "";
    }
    return "/data/data/" + process_name + "/files";
}

// Points the gcov runtime at the files directory of the app before the first
// dump. A GCOV_PREFIX that the test runner sets stays in effect.
inline void SetGcovPrefix() {
#ifdef __ANDROID__
    const std::string prefix = AppFilesDir();
    if (prefix.empty()) {
        return;
    }
    setenv("GCOV_PREFIX", prefix.c_str(), /*overwrite=*/0);
    setenv("GCOV_PREFIX_STRIP", kGcovPrefixStrip, /*overwrite=*/0);
#endif
}

// Writes the in-memory counters to .gcda files in the app files directory.
// The counters include libswappy.so if the process loads it separately.
// Then resets the counters, so that later flushes add up and do not
// overwrite.
inline void FlushGcovCounters() {
    if (!HasGcovRuntime()) {
        return;
    }
    SetGcovPrefix();
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

} // namespace testing
} // namespace agdk
