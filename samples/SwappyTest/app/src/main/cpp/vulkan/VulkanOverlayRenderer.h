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

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "OverlayBatcher.h"
#include "VulkanBuffer.h"
#include "VulkanHandle.h"

namespace samples::vulkan {

class VulkanContext;

class VulkanOverlayRenderer {
public:
    static constexpr size_t kMaxFramesInFlight = 4;

    VulkanOverlayRenderer() = default;
    ~VulkanOverlayRenderer() {
        Destroy();
    }

    VulkanOverlayRenderer(const VulkanOverlayRenderer&) = delete;
    VulkanOverlayRenderer& operator=(const VulkanOverlayRenderer&) = delete;

    /**
     * Creates the R8_UNORM font texture, sampler, descriptor set, alpha-blended
     * triangle and line pipelines, and per-frame dynamic vertex buffers.
     * Returns true on success, false on any Vulkan resource creation failure.
     */
    bool Initialize(VulkanContext* context, VkRenderPass renderPass);

    void Destroy();

    void RecordDraw(VkCommandBuffer commandBuffer, size_t frameSlot, VkExtent2D extent,
                    const samples::OverlayBatcher& batcher);

private:
    bool UploadFontTexture();
    bool CreateDescriptorSet();
    bool CreatePipelines(VkRenderPass renderPass);

    VulkanContext* mContext = nullptr;

    ScopedImage mFontImage;
    ScopedImageView mFontImageView;
    ScopedSampler mFontSampler;

    ScopedDescriptorSetLayout mDescriptorSetLayout;
    ScopedDescriptorPool mDescriptorPool;
    VkDescriptorSet mDescriptorSet = VK_NULL_HANDLE;

    ScopedPipelineLayout mPipelineLayout;
    ScopedPipeline mTrianglePipeline;

    std::array<VulkanBuffer, kMaxFramesInFlight> mTriangleVertexBuffers{};
};

} // namespace samples::vulkan
