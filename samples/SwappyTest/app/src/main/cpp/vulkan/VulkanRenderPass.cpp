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

#include "VulkanRenderPass.h"

#include <cassert>

namespace samples::vulkan::VulkanRenderPass {

static ScopedRenderPass CreateColorRenderPassInternal(VkDevice device, VkFormat colorFormat,
                                                      VkImageLayout finalLayout) {
    assert(device != VK_NULL_HANDLE);
    assert(colorFormat != VK_FORMAT_UNDEFINED);

    VkAttachmentDescription colorAttachment{
            .flags = 0,
            .format = colorFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = finalLayout,
    };

    VkAttachmentReference colorRef{
            .attachment = 0,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass{
            .flags = 0,
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .inputAttachmentCount = 0,
            .pInputAttachments = nullptr,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorRef,
            .pResolveAttachments = nullptr,
            .pDepthStencilAttachment = nullptr,
            .preserveAttachmentCount = 0,
            .pPreserveAttachments = nullptr,
    };

    VkSubpassDependency dependency{
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstAccessMask =
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = 0,
    };

    VkRenderPassCreateInfo renderPassInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .attachmentCount = 1,
            .pAttachments = &colorAttachment,
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = 1,
            .pDependencies = &dependency,
    };

    ScopedRenderPass renderPass;
    VK_CHECK(vkCreateRenderPass(device, &renderPassInfo, nullptr,
                                renderPass.InitializeInto(device)));
    return renderPass;
}

ScopedRenderPass CreateSimplePresentRenderPass(VkDevice device, VkFormat colorFormat) {
    return CreateColorRenderPassInternal(device, colorFormat, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

ScopedRenderPass CreateOffscreenColorRenderPass(VkDevice device, VkFormat colorFormat) {
    return CreateColorRenderPassInternal(device, colorFormat,
                                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

std::vector<ScopedFramebuffer> CreateSwapchainFramebuffers(VkDevice device, VkRenderPass renderPass,
                                                           const VulkanSwapchain& swapchain) {
    assert(device != VK_NULL_HANDLE);
    assert(renderPass != VK_NULL_HANDLE);
    assert(swapchain.IsValid());

    const auto imageViews = swapchain.GetImageViews();
    const VkExtent2D extent = swapchain.GetExtent();

    std::vector<ScopedFramebuffer> framebuffers(imageViews.size());
    for (size_t i = 0; i < imageViews.size(); ++i) {
        const VkImageView attachment = imageViews[i].Get();
        VkFramebufferCreateInfo fbInfo{
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .renderPass = renderPass,
                .attachmentCount = 1,
                .pAttachments = &attachment,
                .width = extent.width,
                .height = extent.height,
                .layers = 1,
        };
        VK_CHECK(vkCreateFramebuffer(device, &fbInfo, nullptr,
                                     framebuffers[i].InitializeInto(device)));
    }
    return framebuffers;
}

} // namespace samples::vulkan::VulkanRenderPass
