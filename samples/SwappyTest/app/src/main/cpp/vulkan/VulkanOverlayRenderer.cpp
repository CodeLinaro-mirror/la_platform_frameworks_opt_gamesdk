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

#include "VulkanOverlayRenderer.h"

#include <cassert>
#include <cstddef>
#include <span>

#include "VulkanContext.h"
#include "VulkanPipeline.h"
#include "VulkanUtils.h"

namespace samples::vulkan {

namespace {

struct OverlayPushConstants {
    float uScreenWidth;
    float uScreenHeight;
};

bool CheckVkResult(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        VK_LOGE("%s failed with %s (%d)", operation, VkResultToString(result),
                static_cast<int>(result));
        return false;
    }
    return true;
}

/*
 * Compiled with Android NDK glslc (-O -mfmt=num) from GLSL 450:
 *
 * #version 450
 * layout(location = 0) in vec2 aPos;
 * layout(location = 1) in vec2 aUv;
 * layout(location = 2) in vec4 aColor;
 * layout(location = 0) out vec2 vUv;
 * layout(location = 1) out vec4 vColor;
 * layout(push_constant) uniform PushConstants {
 *     vec2 uScreenSize;
 * } pc;
 * void main() {
 *     vec2 ndc = (aPos / pc.uScreenSize) * 2.0 - 1.0;
 *     gl_Position = vec4(ndc.x, ndc.y, 0.0, 1.0);
 *     vUv = aUv;
 *     vColor = aColor;
 * }
 */
constexpr uint32_t kOverlayVertSpv[] =
        {0x07230203, 0x00010000, 0x000d000b, 0x00000037, 0x00000000, 0x00020011, 0x00000001,
         0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e,
         0x00000000, 0x00000001, 0x000b000f, 0x00000000, 0x00000004, 0x6e69616d, 0x00000000,
         0x0000000b, 0x00000021, 0x0000002d, 0x0000002e, 0x00000030, 0x00000032, 0x00040047,
         0x0000000b, 0x0000001e, 0x00000000, 0x00030047, 0x0000000d, 0x00000002, 0x00050048,
         0x0000000d, 0x00000000, 0x00000023, 0x00000000, 0x00030047, 0x0000001f, 0x00000002,
         0x00050048, 0x0000001f, 0x00000000, 0x0000000b, 0x00000000, 0x00050048, 0x0000001f,
         0x00000001, 0x0000000b, 0x00000001, 0x00050048, 0x0000001f, 0x00000002, 0x0000000b,
         0x00000003, 0x00050048, 0x0000001f, 0x00000003, 0x0000000b, 0x00000004, 0x00040047,
         0x0000002d, 0x0000001e, 0x00000000, 0x00040047, 0x0000002e, 0x0000001e, 0x00000001,
         0x00040047, 0x00000030, 0x0000001e, 0x00000001, 0x00040047, 0x00000032, 0x0000001e,
         0x00000002, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016,
         0x00000006, 0x00000020, 0x00040017, 0x00000007, 0x00000006, 0x00000002, 0x00040020,
         0x0000000a, 0x00000001, 0x00000007, 0x0004003b, 0x0000000a, 0x0000000b, 0x00000001,
         0x0003001e, 0x0000000d, 0x00000007, 0x00040020, 0x0000000e, 0x00000009, 0x0000000d,
         0x0004003b, 0x0000000e, 0x0000000f, 0x00000009, 0x00040015, 0x00000010, 0x00000020,
         0x00000001, 0x0004002b, 0x00000010, 0x00000011, 0x00000000, 0x00040020, 0x00000012,
         0x00000009, 0x00000007, 0x0004002b, 0x00000006, 0x00000016, 0x40000000, 0x0004002b,
         0x00000006, 0x00000018, 0x3f800000, 0x00040017, 0x0000001b, 0x00000006, 0x00000004,
         0x00040015, 0x0000001c, 0x00000020, 0x00000000, 0x0004002b, 0x0000001c, 0x0000001d,
         0x00000001, 0x0004001c, 0x0000001e, 0x00000006, 0x0000001d, 0x0006001e, 0x0000001f,
         0x0000001b, 0x00000006, 0x0000001e, 0x0000001e, 0x00040020, 0x00000020, 0x00000003,
         0x0000001f, 0x0004003b, 0x00000020, 0x00000021, 0x00000003, 0x0004002b, 0x00000006,
         0x00000028, 0x00000000, 0x00040020, 0x0000002a, 0x00000003, 0x0000001b, 0x00040020,
         0x0000002c, 0x00000003, 0x00000007, 0x0004003b, 0x0000002c, 0x0000002d, 0x00000003,
         0x0004003b, 0x0000000a, 0x0000002e, 0x00000001, 0x0004003b, 0x0000002a, 0x00000030,
         0x00000003, 0x00040020, 0x00000031, 0x00000001, 0x0000001b, 0x0004003b, 0x00000031,
         0x00000032, 0x00000001, 0x0005002c, 0x00000007, 0x00000036, 0x00000018, 0x00000018,
         0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005,
         0x0004003d, 0x00000007, 0x0000000c, 0x0000000b, 0x00050041, 0x00000012, 0x00000013,
         0x0000000f, 0x00000011, 0x0004003d, 0x00000007, 0x00000014, 0x00000013, 0x00050088,
         0x00000007, 0x00000015, 0x0000000c, 0x00000014, 0x0005008e, 0x00000007, 0x00000017,
         0x00000015, 0x00000016, 0x00050083, 0x00000007, 0x0000001a, 0x00000017, 0x00000036,
         0x00050051, 0x00000006, 0x00000025, 0x0000001a, 0x00000000, 0x00050051, 0x00000006,
         0x00000027, 0x0000001a, 0x00000001, 0x00070050, 0x0000001b, 0x00000029, 0x00000025,
         0x00000027, 0x00000028, 0x00000018, 0x00050041, 0x0000002a, 0x0000002b, 0x00000021,
         0x00000011, 0x0003003e, 0x0000002b, 0x00000029, 0x0004003d, 0x00000007, 0x0000002f,
         0x0000002e, 0x0003003e, 0x0000002d, 0x0000002f, 0x0004003d, 0x0000001b, 0x00000033,
         0x00000032, 0x0003003e, 0x00000030, 0x00000033, 0x000100fd, 0x00010038};

/*
 * Compiled with Android NDK glslc (-O -mfmt=num) from GLSL 450:
 *
 * #version 450
 * layout(location = 0) in vec2 vUv;
 * layout(location = 1) in vec4 vColor;
 * layout(set = 0, binding = 0) uniform sampler2D uFontTex;
 * layout(location = 0) out vec4 outColor;
 * void main() {
 *     float mask = texture(uFontTex, vUv).r;
 *     outColor = vec4(vColor.rgb, vColor.a * mask);
 * }
 */
constexpr uint32_t kOverlayFragSpv[] =
        {0x07230203, 0x00010000, 0x000d000a, 0x00000028, 0x00000000, 0x00020011, 0x00000001,
         0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e,
         0x00000000, 0x00000001, 0x0008000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000,
         0x00000010, 0x00000018, 0x0000001a, 0x00030010, 0x00000004, 0x00000007, 0x00040047,
         0x0000000c, 0x00000022, 0x00000000, 0x00040047, 0x0000000c, 0x00000021, 0x00000000,
         0x00040047, 0x00000010, 0x0000001e, 0x00000000, 0x00040047, 0x00000018, 0x0000001e,
         0x00000000, 0x00040047, 0x0000001a, 0x0000001e, 0x00000001, 0x00020013, 0x00000002,
         0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00090019,
         0x00000009, 0x00000006, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000001,
         0x00000000, 0x0003001b, 0x0000000a, 0x00000009, 0x00040020, 0x0000000b, 0x00000000,
         0x0000000a, 0x0004003b, 0x0000000b, 0x0000000c, 0x00000000, 0x00040017, 0x0000000e,
         0x00000006, 0x00000002, 0x00040020, 0x0000000f, 0x00000001, 0x0000000e, 0x0004003b,
         0x0000000f, 0x00000010, 0x00000001, 0x00040017, 0x00000012, 0x00000006, 0x00000004,
         0x00040015, 0x00000014, 0x00000020, 0x00000000, 0x00040020, 0x00000017, 0x00000003,
         0x00000012, 0x0004003b, 0x00000017, 0x00000018, 0x00000003, 0x00040020, 0x00000019,
         0x00000001, 0x00000012, 0x0004003b, 0x00000019, 0x0000001a, 0x00000001, 0x0004002b,
         0x00000014, 0x0000001e, 0x00000003, 0x00040020, 0x0000001f, 0x00000001, 0x00000006,
         0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005,
         0x0004003d, 0x0000000a, 0x0000000d, 0x0000000c, 0x0004003d, 0x0000000e, 0x00000011,
         0x00000010, 0x00050057, 0x00000012, 0x00000013, 0x0000000d, 0x00000011, 0x00050051,
         0x00000006, 0x00000016, 0x00000013, 0x00000000, 0x0004003d, 0x00000012, 0x0000001c,
         0x0000001a, 0x00050041, 0x0000001f, 0x00000020, 0x0000001a, 0x0000001e, 0x0004003d,
         0x00000006, 0x00000021, 0x00000020, 0x00050085, 0x00000006, 0x00000023, 0x00000021,
         0x00000016, 0x00050051, 0x00000006, 0x00000024, 0x0000001c, 0x00000000, 0x00050051,
         0x00000006, 0x00000025, 0x0000001c, 0x00000001, 0x00050051, 0x00000006, 0x00000026,
         0x0000001c, 0x00000002, 0x00070050, 0x00000012, 0x00000027, 0x00000024, 0x00000025,
         0x00000026, 0x00000023, 0x0003003e, 0x00000018, 0x00000027, 0x000100fd, 0x00010038};

} // namespace

bool VulkanOverlayRenderer::Initialize(VulkanContext* context, VkRenderPass renderPass) {
    assert(context != nullptr);
    Destroy();
    mContext = context;

    if (!UploadFontTexture() || !CreateDescriptorSet() || !CreatePipelines(renderPass)) {
        Destroy();
        return false;
    }

    const VkDeviceSize triangleBufferBytes =
            samples::OverlayBatcher::kMaxTriangleVertices * sizeof(samples::OverlayVertex);
    const VkMemoryPropertyFlags hostFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    for (size_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (!mTriangleVertexBuffers[i].Create(mContext, triangleBufferBytes,
                                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, hostFlags)) {
            Destroy();
            return false;
        }
    }
    return true;
}

bool VulkanOverlayRenderer::UploadFontTexture() {
    VkDevice device = mContext->GetDevice();
    constexpr uint32_t atlasW = samples::OverlayBatcher::kFontAtlasWidth;
    constexpr uint32_t atlasH = samples::OverlayBatcher::kFontAtlasHeight;
    constexpr VkDeviceSize imageByteSize = atlasW * atlasH;

    std::array<uint8_t, imageByteSize> atlasPixels{};
    samples::OverlayBatcher::BuildFontAtlasPixels(atlasPixels);

    VulkanBuffer stagingBuffer;
    if (!stagingBuffer.Create(mContext, imageByteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        return false;
    }
    if (!stagingBuffer.UploadData(atlasPixels.data(), imageByteSize)) {
        return false;
    }

    VkImageCreateInfo imageInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8_UNORM,
            .extent = {atlasW, atlasH, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImage rawImage = VK_NULL_HANDLE;
    if (!CheckVkResult(vkCreateImage(device, &imageInfo, nullptr, &rawImage), "vkCreateImage")) {
        return false;
    }
    mFontImage.Reset(device, rawImage);

    VkMemoryRequirements memRequirements{};
    vkGetImageMemoryRequirements(device, mFontImage.Get(), &memRequirements);
    VkDeviceMemory fontMemory = VK_NULL_HANDLE;
    VkDeviceSize fontOffset = 0;
    if (!mContext->AllocateMemory(memRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &fontMemory,
                                  &fontOffset)) {
        return false;
    }
    if (!CheckVkResult(vkBindImageMemory(device, mFontImage.Get(), fontMemory, fontOffset),
                       "vkBindImageMemory(font)")) {
        return false;
    }

    VkCommandBuffer uploadCommandBuffer = mContext->CreateCommandBuffer();
    if (uploadCommandBuffer == VK_NULL_HANDLE) {
        return false;
    }
    VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
    };
    if (!CheckVkResult(vkBeginCommandBuffer(uploadCommandBuffer, &beginInfo),
                       "vkBeginCommandBuffer(font)")) {
        mContext->FreeCommandBuffer(uploadCommandBuffer);
        return false;
    }

    VkImageMemoryBarrier toTransferBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = mFontImage.Get(),
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(uploadCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toTransferBarrier);

    VkBufferImageCopy copyRegion{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageOffset = {0, 0, 0},
            .imageExtent = {atlasW, atlasH, 1},
    };
    vkCmdCopyBufferToImage(uploadCommandBuffer, stagingBuffer.GetBuffer(), mFontImage.Get(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    VkImageMemoryBarrier toShaderReadBarrier = toTransferBarrier;
    toShaderReadBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderReadBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(uploadCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toShaderReadBarrier);

    if (!CheckVkResult(vkEndCommandBuffer(uploadCommandBuffer), "vkEndCommandBuffer(font)")) {
        mContext->FreeCommandBuffer(uploadCommandBuffer);
        return false;
    }

    VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &uploadCommandBuffer,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr,
    };
    const bool submitted = CheckVkResult(vkQueueSubmit(mContext->GetGraphicsQueue(), 1, &submitInfo,
                                                       VK_NULL_HANDLE),
                                         "vkQueueSubmit(font)") &&
            CheckVkResult(vkQueueWaitIdle(mContext->GetGraphicsQueue()), "vkQueueWaitIdle(font)");
    mContext->FreeCommandBuffer(uploadCommandBuffer);
    stagingBuffer.Destroy();
    if (!submitted) {
        return false;
    }

    VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = mFontImage.Get(),
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R8_UNORM,
            .components =
                    {
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                    },
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };

    VkImageView rawView = VK_NULL_HANDLE;
    if (!CheckVkResult(vkCreateImageView(device, &viewInfo, nullptr, &rawView),
                       "vkCreateImageView(font)")) {
        return false;
    }
    mFontImageView.Reset(device, rawView);

    VkSamplerCreateInfo samplerInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .magFilter = VK_FILTER_NEAREST,
            .minFilter = VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1.0f,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0.0f,
            .maxLod = 1.0f,
            .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
            .unnormalizedCoordinates = VK_FALSE,
    };

    VkSampler rawSampler = VK_NULL_HANDLE;
    if (!CheckVkResult(vkCreateSampler(device, &samplerInfo, nullptr, &rawSampler),
                       "vkCreateSampler(font)")) {
        return false;
    }
    mFontSampler.Reset(device, rawSampler);
    return true;
}

bool VulkanOverlayRenderer::CreateDescriptorSet() {
    VkDevice device = mContext->GetDevice();

    VkDescriptorSetLayoutBinding samplerBinding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = 1,
            .pBindings = &samplerBinding,
    };

    VkDescriptorSetLayout rawSetLayout = VK_NULL_HANDLE;
    if (!CheckVkResult(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &rawSetLayout),
                       "vkCreateDescriptorSetLayout")) {
        return false;
    }
    mDescriptorSetLayout.Reset(device, rawSetLayout);

    VkDescriptorPoolSize poolSize{
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
    };

    VkDescriptorPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize,
    };

    VkDescriptorPool rawPool = VK_NULL_HANDLE;
    if (!CheckVkResult(vkCreateDescriptorPool(device, &poolInfo, nullptr, &rawPool),
                       "vkCreateDescriptorPool")) {
        return false;
    }
    mDescriptorPool.Reset(device, rawPool);

    VkDescriptorSetLayout setLayoutHandle = mDescriptorSetLayout.Get();
    VkDescriptorSetAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = mDescriptorPool.Get(),
            .descriptorSetCount = 1,
            .pSetLayouts = &setLayoutHandle,
    };

    if (!CheckVkResult(vkAllocateDescriptorSets(device, &allocInfo, &mDescriptorSet),
                       "vkAllocateDescriptorSets")) {
        return false;
    }

    VkDescriptorImageInfo imageDescriptor{
            .sampler = mFontSampler.Get(),
            .imageView = mFontImageView.Get(),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet descriptorWrite{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = mDescriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &imageDescriptor,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr,
    };

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    return true;
}

bool VulkanOverlayRenderer::CreatePipelines(VkRenderPass renderPass) {
    VkDevice device = mContext->GetDevice();

    ScopedShaderModule vertModule = VulkanPipeline::CreateShaderModule(device, kOverlayVertSpv);
    ScopedShaderModule fragModule = VulkanPipeline::CreateShaderModule(device, kOverlayFragSpv);
    if (!vertModule.IsValid() || !fragModule.IsValid()) {
        return false;
    }

    VkPushConstantRange pushConstantRange{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(OverlayPushConstants),
    };
    VkDescriptorSetLayout setLayoutHandle = mDescriptorSetLayout.Get();
    mPipelineLayout = VulkanPipeline::
            CreatePipelineLayout(device,
                                 std::span<const VkPushConstantRange>(&pushConstantRange, 1),
                                 std::span<const VkDescriptorSetLayout>(&setLayoutHandle, 1));
    if (!mPipelineLayout.IsValid()) {
        return false;
    }

    VkVertexInputBindingDescription bindingDesc{
            .binding = 0,
            .stride = sizeof(samples::OverlayVertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    VkVertexInputAttributeDescription attribDescs[3]{
            {
                    .location = 0,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32_SFLOAT,
                    .offset = offsetof(samples::OverlayVertex, x),
            },
            {
                    .location = 1,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32_SFLOAT,
                    .offset = offsetof(samples::OverlayVertex, u),
            },
            {
                    .location = 2,
                    .binding = 0,
                    .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                    .offset = offsetof(samples::OverlayVertex, r),
            },
    };

    VulkanPipelineBuilder builder;
    builder.SetShaderStages(vertModule.Get(), fragModule.Get())
            .SetVertexInput(std::span<const VkVertexInputBindingDescription>(&bindingDesc, 1),
                            std::span<const VkVertexInputAttributeDescription>(attribDescs, 3))
            .SetInputAssembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .SetRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
            .SetAlphaBlending(true)
            .SetLayout(mPipelineLayout.Get())
            .SetRenderPass(renderPass, 0);

    mTrianglePipeline = builder.Build(device);
    return mTrianglePipeline.IsValid();
}

void VulkanOverlayRenderer::Destroy() {
    for (auto& buffer : mTriangleVertexBuffers) {
        buffer.Destroy();
    }
    mTrianglePipeline.Reset();
    mPipelineLayout.Reset();
    mDescriptorPool.Reset();
    mDescriptorSetLayout.Reset();
    mDescriptorSet = VK_NULL_HANDLE;
    mFontSampler.Reset();
    mFontImageView.Reset();
    mFontImage.Reset();
    mContext = nullptr;
}

void VulkanOverlayRenderer::RecordDraw(VkCommandBuffer commandBuffer, size_t frameSlot,
                                       VkExtent2D extent, const samples::OverlayBatcher& batcher) {
    if (!mTrianglePipeline.IsValid()) {
        return;
    }
    const auto triangleVertices = batcher.GetTriangleVertices();
    if (triangleVertices.empty()) {
        return;
    }

    const size_t slot = frameSlot % kMaxFramesInFlight;
    const OverlayPushConstants pushConstants{
            static_cast<float>(extent.width),
            static_cast<float>(extent.height),
    };
    VkDescriptorSet descriptorSet = mDescriptorSet;

    if (mTriangleVertexBuffers[slot].UploadData(triangleVertices.data(),
                                                triangleVertices.size_bytes())) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mTrianglePipeline.Get());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                mPipelineLayout.Get(), 0, 1, &descriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, mPipelineLayout.Get(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(OverlayPushConstants), &pushConstants);
        VkBuffer vertexBuffers[] = {mTriangleVertexBuffers[slot].GetBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(triangleVertices.size()), 1, 0, 0);
    }
}

} // namespace samples::vulkan
