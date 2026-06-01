#pragma once

#include <android/log.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <stdbool.h>

#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan.h>
#undef VK_NO_PROTOTYPES

// Struct for passing state from java app to native app.
struct android_app_state {
    ANativeWindow* window;
    JavaVM* vm;
    jobject clazz;
    bool running;
    bool destroyRequested;
};

// Start the cube application's render loop.
void main_loop(struct android_app_state*);

// Update the amount of GPU work done each frame.
void update_gpu_workload(int32_t new_workload);

// Update the amount of CPU work done each frame.
void update_cpu_workload(int32_t new_workload);

// Get the current active swapchain.
VkSwapchainKHR get_current_swapchain();

// Get the target IPD (image present duration) in nanoseconds.
uint64_t get_target_ipd();

// Set the application options before starting the main loop.
void set_options(bool display_timing_enabled, bool present_timing_enabled, bool swappy_enabled,
                 bool set_30_fps_limit);

// Get the duration of the last rendered frame in nanoseconds.
uint64_t get_last_frame_duration_ns();

// Get the refresh cycle duration in nanoseconds.
uint64_t get_refresh_duration_ns();
