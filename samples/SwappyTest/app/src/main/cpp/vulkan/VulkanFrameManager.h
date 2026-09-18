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

#include <cstdint>
#include <vector>

#include "VulkanHandle.h"
#include "VulkanSwapchain.h"
#include "VulkanUtils.h"

namespace samples::vulkan {

class VulkanContext;

/**
 * Manages per-frame synchronization primitives (fences, semaphores) and command buffers.
 */
class VulkanFrameManager {
public:
    VulkanFrameManager() = default;
    ~VulkanFrameManager();

    VulkanFrameManager(const VulkanFrameManager&) = delete;
    VulkanFrameManager& operator=(const VulkanFrameManager&) = delete;
    VulkanFrameManager(VulkanFrameManager&&) = delete;
    VulkanFrameManager& operator=(VulkanFrameManager&&) = delete;

    void Initialize(VulkanContext* context, size_t framesInFlight, size_t swapchainImageCount);

    void Destroy();

    void EnsureRenderFinishedSemaphores(size_t count);

    [[nodiscard]] VkCommandBuffer BeginFrame(VulkanSwapchain* swapchain, uint32_t* outImageIndex);

    [[nodiscard]] VkResult EndFrame(VulkanSwapchain* swapchain, uint32_t imageIndex,
                                    VkCommandBuffer commandBuffer, bool useSwappy);

private:
    struct FrameResources {
        ScopedSemaphore imageAvailableSemaphore;
        ScopedFence inFlightFence;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    };

    VulkanContext* mContext = nullptr;
    std::vector<FrameResources> mFrames;
    std::vector<ScopedSemaphore> mRenderFinishedSemaphores;
    size_t mCurrentFrame = 0;
    bool mSuboptimalAcquire = false;
};

} // namespace samples::vulkan
