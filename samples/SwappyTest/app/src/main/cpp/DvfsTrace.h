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

// SwappyTest's ATrace helpers for frame, workload, step, phase, and run slices.

#include <stdint.h>
#include <stdio.h>

#include <cinttypes>

#include "Trace.h"

namespace dvfs {

struct [[nodiscard]] ScopedFormattedTrace {
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
    ScopedFormattedTrace(const ScopedFormattedTrace&) = delete;
    ScopedFormattedTrace& operator=(const ScopedFormattedTrace&) = delete;
    bool mActive = false;
};

inline ScopedFormattedTrace trace_frame(uint32_t id) {
    return ScopedFormattedTrace("SwappyTest::Frame|%" PRIu32, id);
}

inline ScopedFormattedTrace trace_cpu_work(uint32_t id) {
    return ScopedFormattedTrace("SwappyTest::CpuWork|%" PRIu32, id);
}

inline ScopedFormattedTrace trace_gpu_submit(uint32_t id) {
    return ScopedFormattedTrace("SwappyTest::GpuSubmit|%" PRIu32, id);
}

// Traces the worst CPU headroom as a counter, only when it changed unless
// forced, so the run-start and run-end samples always land in the trace.
void trace_cpu_headroom_pct(int pct, bool force);

// A zero-length slice on the calling thread: ATrace has no instant event, and
// trace_processor imports a begin immediately followed by an end as one.
void trace_instant(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

void trace_counter(const char* name, int64_t value);

void trace_end_step();
void trace_begin_step(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void trace_update_phase(const char* phase_tag);
void trace_start_run(const char* scenario, int iter);
void trace_end_run();

} // namespace dvfs
