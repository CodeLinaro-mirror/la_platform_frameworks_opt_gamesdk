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
#include <span>
#include <vector>

#include "VulkanHandle.h"
#include "VulkanUtils.h"

namespace samples::vulkan {

namespace VulkanPipeline {

[[nodiscard]] ScopedShaderModule CreateShaderModule(VkDevice device,
                                                    std::span<const uint32_t> spirvCode);

[[nodiscard]] ScopedPipelineLayout CreatePipelineLayout(
        VkDevice device, std::span<const VkPushConstantRange> pushConstantRanges,
        std::span<const VkDescriptorSetLayout> setLayouts = {});

} // namespace VulkanPipeline

/**
 * Fluent builder for Vulkan graphics pipelines with dynamic viewport and scissor state.
 */
class VulkanPipelineBuilder {
public:
    VulkanPipelineBuilder();

    VulkanPipelineBuilder& SetShaderStages(VkShaderModule vertexShader,
                                           VkShaderModule fragmentShader);

    VulkanPipelineBuilder& SetVertexInput(
            std::span<const VkVertexInputBindingDescription> bindings,
            std::span<const VkVertexInputAttributeDescription> attributes);

    VulkanPipelineBuilder& SetInputAssembly(VkPrimitiveTopology topology,
                                            VkBool32 primitiveRestartEnable = VK_FALSE);

    VulkanPipelineBuilder& SetRasterization(
            VkPolygonMode polygonMode, VkCullModeFlags cullMode,
            VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE);

    VulkanPipelineBuilder& SetAlphaBlending(bool enable);

    VulkanPipelineBuilder& SetRenderPass(VkRenderPass renderPass, uint32_t subpass = 0);
    VulkanPipelineBuilder& SetLayout(VkPipelineLayout layout);

    [[nodiscard]] ScopedPipeline Build(VkDevice device) const;

private:
    std::vector<VkPipelineShaderStageCreateInfo> mShaderStages;
    std::vector<VkVertexInputBindingDescription> mVertexBindings;
    std::vector<VkVertexInputAttributeDescription> mVertexAttributes;
    VkPipelineInputAssemblyStateCreateInfo mInputAssembly{};
    VkPipelineRasterizationStateCreateInfo mRasterizer{};
    VkPipelineMultisampleStateCreateInfo mMultisampling{};
    VkPipelineColorBlendAttachmentState mColorBlendAttachment{};
    VkRenderPass mRenderPass = VK_NULL_HANDLE;
    uint32_t mSubpass = 0;
    VkPipelineLayout mPipelineLayout = VK_NULL_HANDLE;
};

} // namespace samples::vulkan
