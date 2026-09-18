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

// CPU and GPU frequency sampling from sysfs. Descriptors and the per-CPU
// ceilings are opened and cached once, so the per-frame samples cost a
// single pread().

namespace dvfs {

// Size of every table indexed by CPU id.
constexpr int kMaxCpus = 32;

// kHz to MHz, keeping -1 for a frequency that could not be read.
inline int khz_to_mhz(int khz) {
    return (khz > 0) ? (khz / 1000) : -1;
}

// part as a percentage of whole, capped at 100.
inline float clamped_pct(float part, float whole) {
    const float pct = (part / whole) * 100.0f;
    return (pct > 100.0f) ? 100.0f : pct;
}

// The number of distinct CPU max-frequency clusters, 3 when unknown.
int cpu_cluster_count(void);

// The cluster of cpu, 0 = Little ... last = Big/Prime.
int cpu_cluster_of(int cpu);

// Frequencies in kHz, -1 when unreadable.
int read_cpuinfo_max_freq_khz(int cpu);
int read_cpu_freq_khz(int cpu);
int find_device_global_max_freq_khz(void);
int read_gpu_freq_khz(void);
int read_gpu_max_freq_khz(void);

// The lowest scaling_max_freq / cpuinfo_max_freq ratio across CPUs, in
// percent, -1 when unreadable.
int worst_cpu_headroom_pct(void);

} // namespace dvfs
