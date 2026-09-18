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

#include "../Circle.h"
#include "VulkanBuffer.h"
#include "VulkanContext.h"
#include "VulkanHandle.h"
#include "VulkanSwapchain.h"

namespace samples {
class OverlayBatcher;
}

namespace samples::vulkan {

class VulkanOverlayRenderer;

/**
 * Renders the SwappyTest bouncing circle and calibrated fragment-shader GPU workload
 * using Vulkan.
 */
class VulkanCircleRenderer {
public:
    VulkanCircleRenderer() = default;
    ~VulkanCircleRenderer() {
        Destroy();
    }

    VulkanCircleRenderer(const VulkanCircleRenderer&) = delete;
    VulkanCircleRenderer& operator=(const VulkanCircleRenderer&) = delete;

    [[nodiscard]] bool Initialize(VulkanContext& context, const VulkanSwapchain& swapchain);
    void RecreateFramebuffers(VkDevice device, const VulkanSwapchain& swapchain);
    void Destroy() noexcept;

    [[nodiscard]] VkRenderPass GetRenderPass() const noexcept {
        return mRenderPass.Get();
    }

    void RecordDraw(VkCommandBuffer commandBuffer, uint32_t imageIndex, VkExtent2D extent,
                    float aspectRatio, const std::vector<samples::Circle>& circles,
                    int32_t gpuWorkload, VulkanOverlayRenderer* overlayRenderer = nullptr,
                    size_t frameSlot = 0,
                    const samples::OverlayBatcher* overlayBatcher = nullptr) const;

    void RecordOffscreenDraw(VkCommandBuffer commandBuffer, VkRenderPass renderPass,
                             VkFramebuffer framebuffer, VkExtent2D extent, float aspectRatio,
                             const std::vector<samples::Circle>& circles,
                             int32_t gpuWorkload) const;

private:
    static constexpr uint32_t kSegments = 36;
    static constexpr uint32_t kVertexCount = kSegments * 3;

    void RecordCircles(VkCommandBuffer commandBuffer, VkExtent2D extent, float aspectRatio,
                       const std::vector<samples::Circle>& circles, int32_t gpuWorkload) const;

    VulkanBuffer mVertexBuffer;
    ScopedRenderPass mRenderPass;
    ScopedPipelineLayout mPipelineLayout;
    ScopedPipeline mPipeline;
    std::vector<ScopedFramebuffer> mFramebuffers;
};

} // namespace samples::vulkan
