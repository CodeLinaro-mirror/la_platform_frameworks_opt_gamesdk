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

#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif

#include <android/log.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <cstdlib>

namespace samples::vulkan {

inline constexpr const char* kVulkanLogTag = "SwappyTestVulkan";

#define VK_LOGI(...) \
    __android_log_print(ANDROID_LOG_INFO, ::samples::vulkan::kVulkanLogTag, __VA_ARGS__)
#define VK_LOGW(...) \
    __android_log_print(ANDROID_LOG_WARN, ::samples::vulkan::kVulkanLogTag, __VA_ARGS__)
#define VK_LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, ::samples::vulkan::kVulkanLogTag, __VA_ARGS__)

const char* VkResultToString(VkResult result) noexcept;

#define VK_CHECK(call)                                                                    \
    do {                                                                                  \
        const VkResult vkCheckResult = (call);                                            \
        if (vkCheckResult < VK_SUCCESS) {                                                 \
            VK_LOGE("Vulkan error in %s at %s:%d -> %s (%d)", #call, __FILE__, __LINE__,  \
                    ::samples::vulkan::VkResultToString(vkCheckResult),                   \
                    static_cast<int>(vkCheckResult));                                     \
            std::abort();                                                                 \
        } else if (vkCheckResult != VK_SUCCESS) {                                         \
            VK_LOGW("Vulkan non-zero success in %s at %s:%d -> %s (%d)", #call, __FILE__, \
                    __LINE__, ::samples::vulkan::VkResultToString(vkCheckResult),         \
                    static_cast<int>(vkCheckResult));                                     \
        }                                                                                 \
    } while (false)

void SetViewportAndScissor(VkCommandBuffer commandBuffer, VkExtent2D extent) noexcept;

} // namespace samples::vulkan
