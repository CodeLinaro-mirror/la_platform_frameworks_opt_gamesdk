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

#include "CpuGpuFreq.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <iterator>
#include <mutex>

namespace dvfs {

// The integer a sysfs node holds right now. sysfs regenerates the content on
// every read from offset zero, so the descriptor never needs reopening or
// rewinding. -1 when the node is unreadable; one that failed to open is -1
// without costing the frame a syscall.
static int pread_sysfs_int(int fd) {
    if (fd < 0) return -1;
    char text[32];
    const ssize_t length = pread(fd, text, sizeof(text) - 1, 0);
    if (length <= 0) return -1;
    text[length] = '\0';
    char* end = nullptr;
    const long value = strtol(text, &end, 10);
    return (end == text) ? -1 : (int)value;
}

static int s_cpu_max_freq_khz[kMaxCpus];
static int s_cluster_max_freqs_khz[8];
static int s_num_cpu_clusters = 0;
static int s_cpu_to_cluster[kMaxCpus];
static std::once_flag s_cpu_max_freq_cache_once;

static void load_cpu_max_freq_cache(void) {
    for (int c = 0; c < kMaxCpus; ++c) {
        s_cpu_to_cluster[c] = 0;
        char path[128];
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", c);
        FILE* fp = fopen(path, "r");
        if (fp) {
            int val = 0;
            if (fscanf(fp, "%d", &val) == 1 && val > 0) {
                s_cpu_max_freq_khz[c] = val;
                bool found = false;
                for (int k = 0; k < s_num_cpu_clusters; ++k) {
                    if (s_cluster_max_freqs_khz[k] == val) {
                        found = true;
                        break;
                    }
                }
                if (!found && s_num_cpu_clusters < 8) {
                    s_cluster_max_freqs_khz[s_num_cpu_clusters++] = val;
                }
            } else {
                s_cpu_max_freq_khz[c] = -1;
            }
            fclose(fp);
        } else {
            s_cpu_max_freq_khz[c] = -1;
        }
    }
    // Sort unique cluster max frequencies ascending (0 = Little, ..., last = Big/Prime)
    for (int i = 0; i < s_num_cpu_clusters; ++i) {
        for (int j = i + 1; j < s_num_cpu_clusters; ++j) {
            if (s_cluster_max_freqs_khz[i] > s_cluster_max_freqs_khz[j]) {
                int tmp = s_cluster_max_freqs_khz[i];
                s_cluster_max_freqs_khz[i] = s_cluster_max_freqs_khz[j];
                s_cluster_max_freqs_khz[j] = tmp;
            }
        }
    }
    if (s_num_cpu_clusters > 0) {
        for (int c = 0; c < kMaxCpus; ++c) {
            int val = s_cpu_max_freq_khz[c];
            if (val > 0) {
                for (int k = 0; k < s_num_cpu_clusters; ++k) {
                    if (s_cluster_max_freqs_khz[k] == val) {
                        s_cpu_to_cluster[c] = k;
                        break;
                    }
                }
            }
        }
    } else {
        // Assume the common 4 little + 3 mid + 1 prime layout.
        constexpr int kFallbackFirstMidCpu = 4;
        constexpr int kFallbackFirstPrimeCpu = 7;
        s_num_cpu_clusters = 3;
        for (int c = 0; c < kMaxCpus; ++c) {
            s_cpu_to_cluster[c] = (c < kFallbackFirstMidCpu) ? 0
                    : (c < kFallbackFirstPrimeCpu)           ? 1
                                                             : 2;
        }
    }
}

// Loads the cache exactly once, however many threads race to be first.
static void init_cpu_max_freq_cache(void) {
    std::call_once(s_cpu_max_freq_cache_once, load_cpu_max_freq_cache);
}

int cpu_cluster_count(void) {
    init_cpu_max_freq_cache();
    return s_num_cpu_clusters > 0 ? s_num_cpu_clusters : 3;
}

int cpu_cluster_of(int cpu) {
    init_cpu_max_freq_cache();
    return (cpu >= 0 && cpu < kMaxCpus) ? s_cpu_to_cluster[cpu] : 0;
}

int read_cpuinfo_max_freq_khz(int cpu) {
    init_cpu_max_freq_cache();
    if (cpu >= 0 && cpu < kMaxCpus) {
        return s_cpu_max_freq_khz[cpu];
    }
    return -1;
}

// Descriptors on the cpufreq nodes that are sampled inside the timed frame,
// held open for the life of the process so a sample costs a single pread()
// instead of an open(), read() and close(). -1 where the node cannot be
// opened.
static int s_cpu_cur_freq_fds[kMaxCpus];
static int s_cpu_scaling_max_freq_fds[kMaxCpus];
static std::once_flag s_cpu_freq_fds_once;

static int open_cpufreq_node(int cpu, const char* node) {
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/%s", cpu, node);
    return open(path, O_RDONLY | O_CLOEXEC);
}

static void open_cpu_freq_fds(void) {
    for (int cpu = 0; cpu < kMaxCpus; ++cpu) {
        s_cpu_cur_freq_fds[cpu] = open_cpufreq_node(cpu, "scaling_cur_freq");
        if (s_cpu_cur_freq_fds[cpu] < 0) {
            s_cpu_cur_freq_fds[cpu] = open_cpufreq_node(cpu, "cpuinfo_cur_freq");
        }
        s_cpu_scaling_max_freq_fds[cpu] = open_cpufreq_node(cpu, "scaling_max_freq");
    }
}

int read_cpu_freq_khz(int cpu) {
    std::call_once(s_cpu_freq_fds_once, open_cpu_freq_fds);
    if (cpu < 0 || cpu >= kMaxCpus) return -1;
    return pread_sysfs_int(s_cpu_cur_freq_fds[cpu]);
}

static int read_scaling_max_freq_khz(int cpu) {
    std::call_once(s_cpu_freq_fds_once, open_cpu_freq_fds);
    if (cpu < 0 || cpu >= kMaxCpus) return -1;
    return pread_sysfs_int(s_cpu_scaling_max_freq_fds[cpu]);
}

int find_device_global_max_freq_khz(void) {
    init_cpu_max_freq_cache();
    int global_max = -1;
    for (int cpu = 0; cpu < kMaxCpus; ++cpu) {
        int max_khz = s_cpu_max_freq_khz[cpu];
        if (max_khz > global_max) {
            global_max = max_khz;
        }
    }
    return global_max;
}

static const char* const GPU_CUR_FREQ_PATHS[] = {
        "/sys/devices/platform/1f000000.mali/cur_freq",
        "/sys/devices/platform/1c500000.mali/cur_freq",
        "/sys/devices/platform/mali/cur_freq",
        "/sys/class/kgsl/kgsl-3d0/gpuclk",
        "/sys/class/kgsl/kgsl-3d0/devfreq/cur_freq",
        "/sys/class/devfreq/gpufreq/cur_freq",
        "/sys/class/devfreq/1c00000.qcom,kgsl-3d0/cur_freq",
        "/sys/class/devfreq/3d00000.qcom,kgsl-3d0/cur_freq",
        "/sys/kernel/gpu/gpu_clock",
};

static const char* const GPU_AVAIL_FREQ_PATHS[] = {
        "/sys/devices/platform/1f000000.mali/available_frequencies",
        "/sys/devices/platform/1c500000.mali/available_frequencies",
        "/sys/devices/platform/mali/available_frequencies",
        "/sys/class/kgsl/kgsl-3d0/gpu_available_frequencies",
        "/sys/class/devfreq/gpufreq/available_frequencies",
};

static const char* const GPU_MAX_FREQ_PATHS[] = {
        "/sys/devices/platform/1f000000.mali/max_freq",
        "/sys/devices/platform/1c500000.mali/max_freq",
        "/sys/devices/platform/mali/max_freq",
        "/sys/class/kgsl/kgsl-3d0/max_gpuclk",
        "/sys/class/kgsl/kgsl-3d0/devfreq/max_freq",
        "/sys/class/devfreq/gpufreq/max_freq",
};

// Some kernels report the GPU clock in Hz rather than kHz.
static int normalize_gpu_khz(int freq) {
    return (freq > 10000000) ? (freq / 1000) : freq;
}

// Held open for the life of the process so each frame samples it with a
// single pread(). -1 when no candidate node opens.
static int s_gpu_cur_freq_fd = -1;
static int s_gpu_max_freq_khz = -1;
static std::once_flag s_gpu_freq_paths_once;

static void open_gpu_freq_paths(void) {
    for (size_t i = 0; i < std::size(GPU_CUR_FREQ_PATHS); ++i) {
        s_gpu_cur_freq_fd = open(GPU_CUR_FREQ_PATHS[i], O_RDONLY | O_CLOEXEC);
        if (s_gpu_cur_freq_fd >= 0) break;
    }

    for (size_t i = 0; i < std::size(GPU_AVAIL_FREQ_PATHS); ++i) {
        FILE* fp = fopen(GPU_AVAIL_FREQ_PATHS[i], "r");
        if (fp) {
            int freq = 0;
            while (fscanf(fp, "%d", &freq) == 1) {
                freq = normalize_gpu_khz(freq);
                if (freq > s_gpu_max_freq_khz) {
                    s_gpu_max_freq_khz = freq;
                }
            }
            fclose(fp);
            if (s_gpu_max_freq_khz > 0) break;
        }
    }

    if (s_gpu_max_freq_khz <= 0) {
        for (size_t i = 0; i < std::size(GPU_MAX_FREQ_PATHS); ++i) {
            FILE* fp = fopen(GPU_MAX_FREQ_PATHS[i], "r");
            if (fp) {
                int freq = 0;
                if (fscanf(fp, "%d", &freq) == 1) {
                    freq = normalize_gpu_khz(freq);
                    if (freq > 0) {
                        s_gpu_max_freq_khz = freq;
                        fclose(fp);
                        break;
                    }
                }
                fclose(fp);
            }
        }
    }
}

int read_gpu_freq_khz(void) {
    std::call_once(s_gpu_freq_paths_once, open_gpu_freq_paths);
    const int val = normalize_gpu_khz(pread_sysfs_int(s_gpu_cur_freq_fd));
    if (val > s_gpu_max_freq_khz) {
        s_gpu_max_freq_khz = val;
    }
    return val;
}

int read_gpu_max_freq_khz(void) {
    std::call_once(s_gpu_freq_paths_once, open_gpu_freq_paths);
    return s_gpu_max_freq_khz;
}

int worst_cpu_headroom_pct(void) {
    init_cpu_max_freq_cache();
    int worst = -1;
    for (int cpu = 0; cpu < kMaxCpus; ++cpu) {
        const int ceiling = s_cpu_max_freq_khz[cpu];
        if (ceiling <= 0) continue;
        const int allowed = read_scaling_max_freq_khz(cpu);
        if (allowed <= 0) continue;
        int pct = (int)((int64_t)allowed * 100 / ceiling);
        if (pct > 100) pct = 100;
        if (worst < 0 || pct < worst) worst = pct;
    }
    return worst;
}

} // namespace dvfs
