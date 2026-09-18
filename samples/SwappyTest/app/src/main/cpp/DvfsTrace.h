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

// SwappyTest's trace layer: the SWAPPY_TEST_* macros and the ATrace helpers
// behind them. With tracing compiled out the macros expand to nothing.

#include <stdint.h>
#include <stdio.h>

#include <cinttypes>

#include "Trace.h"

namespace dvfs {

// Traces the worst CPU headroom as a counter, only when it changed unless
// forced, so the run-start and run-end samples always land in the trace.
void trace_cpu_headroom_pct(int pct, bool force);

} // namespace dvfs

#if (defined(TRACE) && TRACE == 0) && !defined(SWAPPY_ENABLE_TRACE)
#define SWAPPY_TEST_TRACE_FRAME(id)
#define SWAPPY_TEST_TRACE_CPU_WORK(id)
#define SWAPPY_TEST_TRACE_GPU_SUBMIT(id)
#define SWAPPY_TEST_START_RUN(scenario, iter)
#define SWAPPY_TEST_END_RUN()
#define SWAPPY_TEST_UPDATE_PHASE(tag)
#define SWAPPY_TEST_TRACE_ENABLED() false
#define SWAPPY_TEST_TRACE_INSTANT(...) ((void)0)
#define SWAPPY_TEST_TRACE_COUNTER(name, value) ((void)0)
#define SWAPPY_TEST_BEGIN_STEP(...) ((void)0)
#define SWAPPY_TEST_END_STEP() ((void)0)
#else
namespace dvfs {

struct ScopedFormattedTrace {
    ScopedFormattedTrace(const char* fmt, uint32_t id) {
        if (auto* trace = ::samples::Trace::getInstance()) {
            if (trace->isEnabled()) {
                char buf[64];
                snprintf(buf, sizeof(buf), fmt, id);
                trace->beginSection(buf);
                mActive = true;
            }
        }
    }
    ~ScopedFormattedTrace() {
        if (mActive) {
            if (auto* trace = ::samples::Trace::getInstance()) {
                trace->endSection();
            }
        }
    }
    bool mActive = false;
};

bool is_test_tracing_enabled(void);

// A zero-length slice on the calling thread: ATrace has no instant event, and
// trace_processor imports a begin immediately followed by an end as one.
void trace_instant(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

void trace_counter(const char* name, int64_t value);

void end_step_trace(void);
void begin_step_trace(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void update_phase_trace(const char* phase_tag);
void start_run_trace(const char* scenario, int iter);
void end_run_trace();

} // namespace dvfs

#define SWAPPY_TEST_TRACE_FRAME(id)                                                             \
    ::dvfs::ScopedFormattedTrace PASTE_HELPER(_ftrace_, __LINE__)("SwappyTest::Frame|%" PRIu32, \
                                                                  static_cast<uint32_t>(id))

#define SWAPPY_TEST_TRACE_CPU_WORK(id)                                                            \
    ::dvfs::ScopedFormattedTrace PASTE_HELPER(_ctrace_, __LINE__)("SwappyTest::CpuWork|%" PRIu32, \
                                                                  static_cast<uint32_t>(id))

#define SWAPPY_TEST_TRACE_GPU_SUBMIT(id)                                                  \
    ::dvfs::ScopedFormattedTrace PASTE_HELPER(_gtrace_,                                   \
                                              __LINE__)("SwappyTest::GpuSubmit|%" PRIu32, \
                                                        static_cast<uint32_t>(id))

#define SWAPPY_TEST_UPDATE_PHASE(tag) ::dvfs::update_phase_trace(tag)
#define SWAPPY_TEST_START_RUN(scenario, iter) ::dvfs::start_run_trace(scenario, iter)
#define SWAPPY_TEST_END_RUN() ::dvfs::end_run_trace()
#define SWAPPY_TEST_TRACE_ENABLED() ::dvfs::is_test_tracing_enabled()
#define SWAPPY_TEST_TRACE_INSTANT(...) ::dvfs::trace_instant(__VA_ARGS__)
#define SWAPPY_TEST_TRACE_COUNTER(name, value) ::dvfs::trace_counter(name, value)
#define SWAPPY_TEST_BEGIN_STEP(...) ::dvfs::begin_step_trace(__VA_ARGS__)
#define SWAPPY_TEST_END_STEP() ::dvfs::end_step_trace()
#endif
