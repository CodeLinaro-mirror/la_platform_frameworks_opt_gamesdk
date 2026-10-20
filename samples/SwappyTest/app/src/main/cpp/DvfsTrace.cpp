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

#include "DvfsTrace.h"

#include <stdarg.h>
#include <stdio.h>

#include <atomic>
#include <string>

#include "DvfsBenchmark.h"

namespace dvfs {

void trace_cpu_headroom_pct(int pct, bool force) {
    static std::atomic<int> s_traced_headroom_pct{-1};
    if (pct < 0) return;
    if (s_traced_headroom_pct.exchange(pct, std::memory_order_relaxed) == pct && !force) return;
    trace_counter("SwappyTest.CpuHeadroomPct", pct);
}

// Async section cookies, one per concurrently open SwappyTest async track.
// Async section names separate their fields with ';', never '|': ATrace
// writes them as "S|<pid>|<name>|<cookie>", so trace_processor would take
// the first '|'-separated field as the name and the next as the cookie.
static constexpr int32_t kRunTraceCookie = 0;
static constexpr int32_t kPhaseTraceCookie = 1;
static constexpr int32_t kStepTraceCookie = 2;

// The shared body of the printf-style trace helpers.
static std::string vformat(const char* fmt, va_list args) {
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, args);
    return buf;
}

// A zero-length slice on the calling thread: ATrace has no instant event, and
// trace_processor imports a begin immediately followed by an end as one.
void trace_instant(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const std::string name = vformat(fmt, args);
    va_end(args);
    if (auto* trace = samples::Trace::getInstance()) {
        trace->beginSection(name.c_str());
        trace->endSection();
    }
}

void trace_counter(const char* name, int64_t value) {
    if (auto* trace = samples::Trace::getInstance()) {
        trace->setCounter(name, value);
    }
}

// One async trace track: at most one open section, named by name.
struct AsyncTrack {
    explicit AsyncTrack(int32_t track_cookie) : cookie(track_cookie) {}

    const int32_t cookie;
    std::string name;

    // Ends the open section, if any.
    void end() {
        if (name.empty()) return;
        if (auto* trace = samples::Trace::getInstance()) {
            trace->endAsyncSection(name.c_str(), cookie);
        }
        name.clear();
    }

    // Ends the open section, if any, and opens new_name.
    void begin(std::string new_name) {
        end();
        name = std::move(new_name);
        if (auto* trace = samples::Trace::getInstance()) {
            trace->beginAsyncSection(name.c_str(), cookie);
        }
    }
};

static AsyncTrack s_run_track{kRunTraceCookie};
static AsyncTrack s_phase_track{kPhaseTraceCookie};
static AsyncTrack s_step_track{kStepTraceCookie};

void trace_end_step() {
    s_step_track.end();
}

void trace_begin_step(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    s_step_track.begin(vformat(fmt, args));
    va_end(args);
}

void trace_update_phase(const char* phase_tag) {
    s_phase_track.end();
    if (phase_tag && phase_tag[0] != '\0') {
        s_phase_track.begin(std::string("SwappyTest::Phase;") + phase_tag);
    }
}

void trace_start_run(const char* scenario, int iter) {
    s_step_track.end();
    s_phase_track.end();
    const char* pacing_str = swappy_backend_get_pacing_strategy_name();
    char run_name[128];
    snprintf(run_name, sizeof(run_name), "SwappyMatrix::Run;%s;%s;%d", scenario, pacing_str, iter);
    s_run_track.begin(run_name);
}

void trace_end_run() {
    s_step_track.end();
    s_phase_track.end();
    s_run_track.end();
}

} // namespace dvfs
