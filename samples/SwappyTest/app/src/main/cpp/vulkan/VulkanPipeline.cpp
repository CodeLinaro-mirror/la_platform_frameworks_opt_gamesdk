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

#include "VulkanPipeline.h"

#include <cassert>

namespace samples::vulkan {

namespace VulkanPipeline {

ScopedShaderModule CreateShaderModule(VkDevice device, std::span<const uint32_t> spirvCode) {
    assert(device != VK_NULL_HANDLE);
    assert(!spirvCode.empty());

    VkShaderModuleCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = spirvCode.size_bytes(),
            .pCode = spirvCode.data(),
    };

    ScopedShaderModule module;
    VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, module.InitializeInto(device)));
    return module;
}

ScopedPipelineLayout CreatePipelineLayout(VkDevice device,
                                          std::span<const VkPushConstantRange> pushConstantRanges,
                                          std::span<const VkDescriptorSetLayout> setLayouts) {
    assert(device != VK_NULL_HANDLE);

    VkPipelineLayoutCreateInfo layoutInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
            .pSetLayouts = setLayouts.empty() ? nullptr : setLayouts.data(),
            .pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size()),
            .pPushConstantRanges = pushConstantRanges.empty() ? nullptr : pushConstantRanges.data(),
    };

    ScopedPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, layout.InitializeInto(device)));
    return layout;
}

} // namespace VulkanPipeline

VulkanPipelineBuilder::VulkanPipelineBuilder() {
    mInputAssembly = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
    };

    mRasterizer = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f,
    };

    mMultisampling = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 1.0f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
    };

    mColorBlendAttachment = {
            .blendEnable = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
}

VulkanPipelineBuilder& VulkanPipelineBuilder::SetShaderStages(VkShaderModule vertexShader,
                                                              VkShaderModule fragmentShader) {
    assert(vertexShader != VK_NULL_HANDLE);
    assert(fragmentShader != VK_NULL_HANDLE);

    mShaderStages.clear();
    mShaderStages.push_back({
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertexShader,
            .pName = "main",
            .pSpecializationInfo = nullptr,
    });
    mShaderStages.push_back({
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragmentShader,
            .pName = "main",
            .pSpecializationInfo = nullptr,
    });
    return *this;
}

VulkanPipelineBuilder& VulkanPipelineBuilder::SetVertexInput(
        std::span<const VkVertexInputBindingDescription> bindings,
        std::span<const VkVertexInputAttributeDescription> attributes) {
    mVertexBindings.assign(bindings.begin(), bindings.end());
    mVertexAttributes.assign(attributes.begin(), attributes.end());
    return *this;
}

VulkanPipelineBuilder& VulkanPipelineBuilder::SetInputAssembly(VkPrimitiveTopology topology,
                                                               VkBool32 primitiveRestartEnable) {
    mInputAssembly.topology = topology;
    mInputAssembly.primitiveRestartEnable = primitiveRestartEnable;
    return *this;
}

VulkanPipelineBuilder& VulkanPipelineBuilder::SetRasterization(VkPolygonMode polygonMode,
                                                               VkCullModeFlags cullMode,
                                                               VkFrontFace frontFace) {
    mRasterizer.polygonMode = polygonMode;
    mRasterizer.cullMode = cullMode;
    mRasterizer.frontFace = frontFace;
    return *this;
}

VulkanPipelineBuilder& VulkanPipelineBuilder::SetAlphaBlending(bool enable) {
    if (enable) {
        mColorBlendAttachment.blendEnable = VK_TRUE;
        mColorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        mColorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        mColorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        mColorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        mColorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        mColorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    } else {
        mColorBlendAttachment.blendEnable = VK_FALSE;
        mColorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        mColorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        mColorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        mColorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        mColorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        mColorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    return *this;
}

VulkanPipelineBuilder& VulkanPipelineBuilder::SetRenderPass(VkRenderPass renderPass,
                                                            uint32_t subpass) {
    mRenderPass = renderPass;
    mSubpass = subpass;
    return *this;
}

VulkanPipelineBuilder& VulkanPipelineBuilder::SetLayout(VkPipelineLayout layout) {
    mPipelineLayout = layout;
    return *this;
}

ScopedPipeline VulkanPipelineBuilder::Build(VkDevice device) const {
    assert(device != VK_NULL_HANDLE);
    assert(mPipelineLayout != VK_NULL_HANDLE);
    assert(mRenderPass != VK_NULL_HANDLE);
    assert(!mShaderStages.empty());

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .vertexBindingDescriptionCount = static_cast<uint32_t>(mVertexBindings.size()),
            .pVertexBindingDescriptions =
                    mVertexBindings.empty() ? nullptr : mVertexBindings.data(),
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(mVertexAttributes.size()),
            .pVertexAttributeDescriptions =
                    mVertexAttributes.empty() ? nullptr : mVertexAttributes.data(),
    };

    VkPipelineViewportStateCreateInfo viewportState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .viewportCount = 1,
            .pViewports = nullptr,
            .scissorCount = 1,
            .pScissors = nullptr,
    };

    constexpr VkDynamicState kDynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamicState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .dynamicStateCount = 2,
            .pDynamicStates = kDynamicStates,
    };

    VkPipelineColorBlendStateCreateInfo colorBlending{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .logicOpEnable = VK_FALSE,
            .logicOp = VK_LOGIC_OP_COPY,
            .attachmentCount = 1,
            .pAttachments = &mColorBlendAttachment,
            .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f},
    };

    VkGraphicsPipelineCreateInfo pipelineInfo{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stageCount = static_cast<uint32_t>(mShaderStages.size()),
            .pStages = mShaderStages.data(),
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &mInputAssembly,
            .pTessellationState = nullptr,
            .pViewportState = &viewportState,
            .pRasterizationState = &mRasterizer,
            .pMultisampleState = &mMultisampling,
            .pDepthStencilState = nullptr,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
            .layout = mPipelineLayout,
            .renderPass = mRenderPass,
            .subpass = mSubpass,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1,
    };

    ScopedPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                       pipeline.InitializeInto(device)));
    return pipeline;
}

} // namespace samples::vulkan
