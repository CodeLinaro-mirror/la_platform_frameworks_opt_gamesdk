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

// DvfsBenchmark state and helpers shared between DvfsBenchmark.cpp and
// Calibration.cpp. Not part of the public C API in DvfsBenchmark.h.

#include <stdint.h>
#include <sys/system_properties.h>
#include <time.h>

#include <atomic>
#include <mutex>
#include <string>

namespace dvfs {

inline uint64_t get_time_monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// The value of system property name, or "" when it is unset.
inline std::string read_prop(const char* name) {
    char value[PROP_VALUE_MAX] = {0};
    __system_property_get(name, value);
    return value;
}

inline void sleep_ms(long ms) {
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

// Deterministic CPU workload loop with data dependency and volatile sink
__attribute__((noinline)) inline void execute_cpu_workload(uint64_t iterations) {
    volatile uint64_t sink = 0;
    uint64_t acc = 0x123456789ABCDEF0ULL;
    for (uint64_t i = 0; i < iterations; ++i) {
        acc ^= (i + 0x9E3779B97F4A7C15ULL) + (acc << 6) + (acc >> 2);
    }
    sink = acc;
    (void)sink;
}

inline uint64_t get_thread_cpu_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// dir/filename.
inline std::string path_in(const char* dir, const char* filename) {
    return std::string(dir) + "/" + filename;
}

// The app's external files directory, the default output directory.
inline constexpr char kExternalFilesDir[] = "/sdcard/Android/data/com.samples.swappytest/files";

// Defined in DvfsBenchmark.cpp; see the comments there.
extern std::atomic<int32_t> s_render_surface_width;
extern std::atomic<int32_t> s_render_surface_height;
extern std::atomic<bool> s_calibration_running;
extern std::mutex s_test_mutex;
extern std::atomic<int> s_device_global_max_freq_mhz;
extern std::atomic<int> s_headroom_start_pct;
extern std::atomic<int> s_headroom_end_pct;
extern std::atomic<bool> s_run_tainted;
extern double s_cooldown_duration_ms;

std::string output_path(const char* filename);

} // namespace dvfs
