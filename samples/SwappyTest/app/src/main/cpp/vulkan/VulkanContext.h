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

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "VulkanUtils.h"

namespace samples::vulkan {

/**
 * Manages core Vulkan state: VkInstance, VkPhysicalDevice, VkDevice, VkQueue, and VkCommandPool,
 * including SwappyVk device-extension discovery and queue-family registration.
 */
class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    [[nodiscard]] bool InitializeInstance();
    [[nodiscard]] bool InitializeDevice(VkSurfaceKHR surface);
    void Destroy();
    void WaitIdle() const;

    [[nodiscard]] std::optional<uint32_t> FindMemoryType(
            uint32_t typeFilter, VkMemoryPropertyFlags properties) const noexcept;

    [[nodiscard]] bool AllocateMemory(const VkMemoryRequirements& requirements,
                                      VkMemoryPropertyFlags properties, VkDeviceMemory* outMemory,
                                      VkDeviceSize* outOffset, void** outMappedPtr = nullptr);

    [[nodiscard]] VkCommandBuffer CreateCommandBuffer(
            VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    void FreeCommandBuffer(VkCommandBuffer commandBuffer);

    [[nodiscard]] VkInstance GetInstance() const noexcept {
        return mInstance;
    }
    [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const noexcept {
        return mPhysicalDevice;
    }
    [[nodiscard]] VkDevice GetDevice() const noexcept {
        return mDevice;
    }
    [[nodiscard]] VkQueue GetGraphicsQueue() const noexcept {
        return mGraphicsQueue;
    }
    [[nodiscard]] VkQueue GetPresentQueue() const noexcept {
        return mGraphicsQueue;
    }
    [[nodiscard]] uint32_t GetGraphicsQueueFamily() const noexcept {
        return mGraphicsQueueFamily;
    }
    [[nodiscard]] bool IsInitialized() const noexcept {
        return mDevice != VK_NULL_HANDLE;
    }
    [[nodiscard]] const VkPhysicalDeviceProperties& GetPhysicalDeviceProperties() const noexcept {
        return mPhysicalDeviceProperties;
    }

private:
    struct MemoryArena {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint8_t* mappedBase = nullptr;
        VkDeviceSize capacity = 0;
        VkDeviceSize nextOffset = 0;
    };

    [[nodiscard]] bool PickPhysicalDevice(VkSurfaceKHR surface);
    [[nodiscard]] bool FindQueueFamilies(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                         uint32_t& outQueueFamilyIndex) const;

    VkInstance mInstance = VK_NULL_HANDLE;
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    VkDevice mDevice = VK_NULL_HANDLE;
    VkQueue mGraphicsQueue = VK_NULL_HANDLE;
    uint32_t mGraphicsQueueFamily = UINT32_MAX;
    VkCommandPool mCommandPool = VK_NULL_HANDLE;

    VkPhysicalDeviceProperties mPhysicalDeviceProperties{};
    VkPhysicalDeviceMemoryProperties mMemoryProperties{};
    std::array<MemoryArena, VK_MAX_MEMORY_TYPES> mMemoryArenas{};
};

} // namespace samples::vulkan
