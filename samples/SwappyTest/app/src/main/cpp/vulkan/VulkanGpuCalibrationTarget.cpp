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

#include "VulkanGpuCalibrationTarget.h"

#define LOG_TAG "VulkanGpuCalib"

#include <ctime>
#include <utility>

#include "Log.h"
#include "VulkanCircleRenderer.h"
#include "VulkanContext.h"
#include "VulkanRenderPass.h"

namespace samples::vulkan {

namespace {

static inline int64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

static constexpr uint64_t kGpuPassTimeoutNs = 1000000000ULL; // 1.0 second timeout per pass

} // namespace

VulkanGpuCalibrationTarget::VulkanGpuCalibrationTarget(VulkanContext& context,
                                                       const VulkanCircleRenderer& circleRenderer,
                                                       VkFormat colorFormat, VkExtent2D extent,
                                                       float aspectRatio,
                                                       std::vector<samples::Circle> circles)
      : mContext(context),
        mCircleRenderer(circleRenderer),
        mExtent(extent),
        mAspectRatio(aspectRatio),
        mCircles(std::move(circles)),
        mDevice(context.GetDevice()) {
    if (mExtent.width == 0 || mExtent.height == 0 || colorFormat == VK_FORMAT_UNDEFINED) {
        ALOGE("Invalid extent or format for offscreen Vulkan calibration");
        return;
    }

    // 1. Create dedicated VkImage
    VkImageCreateInfo imageInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = colorFormat,
            .extent = {.width = mExtent.width, .height = mExtent.height, .depth = 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkResult res = vkCreateImage(mDevice, &imageInfo, nullptr, mImage.InitializeInto(mDevice));
    if (res != VK_SUCCESS) {
        ALOGE("Failed to create offscreen VkImage: %d", res);
        return;
    }

    // 2. Allocate dedicated device-local memory (freed when calibration completes)
    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(mDevice, mImage.Get(), &memReqs);

    const auto memTypeIdx =
            mContext.FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!memTypeIdx) {
        ALOGE("Failed to find device-local memory type for offscreen image");
        return;
    }

    VkMemoryAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = memReqs.size,
            .memoryTypeIndex = *memTypeIdx,
    };
    res = vkAllocateMemory(mDevice, &allocInfo, nullptr, mImageMemory.InitializeInto(mDevice));
    if (res != VK_SUCCESS) {
        ALOGE("Failed to allocate device-local memory for offscreen image: %d", res);
        return;
    }

    res = vkBindImageMemory(mDevice, mImage.Get(), mImageMemory.Get(), 0);
    if (res != VK_SUCCESS) {
        ALOGE("Failed to bind offscreen image memory: %d", res);
        return;
    }

    // 3. Create VkImageView
    VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = mImage.Get(),
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = colorFormat,
            .components =
                    {
                            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                    },
            .subresourceRange =
                    {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .baseMipLevel = 0,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1,
                    },
    };

    res = vkCreateImageView(mDevice, &viewInfo, nullptr, mImageView.InitializeInto(mDevice));
    if (res != VK_SUCCESS) {
        ALOGE("Failed to create offscreen VkImageView: %d", res);
        return;
    }

    // 4. Create offscreen color render pass
    mRenderPass = VulkanRenderPass::CreateOffscreenColorRenderPass(mDevice, colorFormat);
    if (!mRenderPass.IsValid()) {
        ALOGE("Failed to create offscreen render pass");
        return;
    }

    // 5. Create VkFramebuffer
    VkImageView attachments[] = {mImageView.Get()};
    VkFramebufferCreateInfo fbInfo{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .renderPass = mRenderPass.Get(),
            .attachmentCount = 1,
            .pAttachments = attachments,
            .width = mExtent.width,
            .height = mExtent.height,
            .layers = 1,
    };

    res = vkCreateFramebuffer(mDevice, &fbInfo, nullptr, mFramebuffer.InitializeInto(mDevice));
    if (res != VK_SUCCESS) {
        ALOGE("Failed to create offscreen VkFramebuffer: %d", res);
        return;
    }

    // 6. Allocate double-buffered command buffers and fences so CPU recording for
    // Pass k+1 overlaps with GPU execution of Pass k.
    VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0, // unsignaled
    };
    for (int i = 0; i < kNumSlots; ++i) {
        mCommandBuffers[i] = mContext.CreateCommandBuffer();
        if (mCommandBuffers[i] == VK_NULL_HANDLE) {
            ALOGE("Failed to allocate command buffer[%d] for calibration", i);
            return;
        }
        res = vkCreateFence(mDevice, &fenceInfo, nullptr, mFences[i].InitializeInto(mDevice));
        if (res != VK_SUCCESS) {
            ALOGE("Failed to create fence[%d] for calibration: %d", i, res);
            return;
        }
    }
}

VulkanGpuCalibrationTarget::~VulkanGpuCalibrationTarget() {
    for (int i = 0; i < kNumSlots; ++i) {
        if (mSlotSubmitted[i] && mFences[i].IsValid()) {
            VkFence fence = mFences[i].Get();
            if (vkWaitForFences(mDevice, 1, &fence, VK_TRUE, kGpuPassTimeoutNs) != VK_SUCCESS) {
                mContext.WaitIdle();
            }
            mSlotSubmitted[i] = false;
        }
        if (mCommandBuffers[i] != VK_NULL_HANDLE) {
            mContext.FreeCommandBuffer(mCommandBuffers[i]);
            mCommandBuffers[i] = VK_NULL_HANDLE;
        }
    }
}

void VulkanGpuCalibrationTarget::PreparePass(int32_t gpuWorkload) {
    if (!IsValid()) {
        return;
    }

    // Choose a slot that is not the currently active GPU slot.
    const int slot = (mActiveSlot >= 0) ? (1 - mActiveSlot) : mWriteSlot;
    mWriteSlot = slot;

    if (mCommandBuffers[slot] == VK_NULL_HANDLE || !mFences[slot].IsValid()) {
        return;
    }

    // Defensive guard: never reset a command buffer that is still in flight.
    if (mSlotSubmitted[slot]) {
        VkFence fence = mFences[slot].Get();
        VkResult waitRes = vkWaitForFences(mDevice, 1, &fence, VK_TRUE, kGpuPassTimeoutNs);
        if (waitRes != VK_SUCCESS) {
            ALOGW("vkWaitForFences in PreparePass timed out or failed: %d", waitRes);
            mSlotRecorded[slot] = false;
            return;
        }
        vkResetFences(mDevice, 1, &fence);
        mSlotSubmitted[slot] = false;
        if (mActiveSlot == slot) {
            mActiveSlot = -1;
        }
    }

    vkResetCommandBuffer(mCommandBuffers[slot], 0);

    VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
    };

    VkResult res = vkBeginCommandBuffer(mCommandBuffers[slot], &beginInfo);
    if (res != VK_SUCCESS) {
        ALOGE("vkBeginCommandBuffer failed: %d", res);
        mSlotRecorded[slot] = false;
        return;
    }

    mCircleRenderer.RecordOffscreenDraw(mCommandBuffers[slot], mRenderPass.Get(),
                                        mFramebuffer.Get(), mExtent, mAspectRatio, mCircles,
                                        gpuWorkload);

    res = vkEndCommandBuffer(mCommandBuffers[slot]);
    if (res != VK_SUCCESS) {
        ALOGE("vkEndCommandBuffer failed: %d", res);
        mSlotRecorded[slot] = false;
        return;
    }

    mSlotRecorded[slot] = true;
    mSlotWorkload[slot] = gpuWorkload;
}

bool VulkanGpuCalibrationTarget::SubmitPass(int32_t gpuWorkload) {
    if (!IsValid()) {
        return false;
    }

    // If a previous pass is still active on the single offscreen framebuffer, drain it first.
    if (mActiveSlot >= 0 && mSlotSubmitted[mActiveSlot]) {
        if (WaitPass().wall_ns < 0) {
            return false;
        }
    }

    int slot = mWriteSlot;
    if (!mSlotRecorded[slot] || mSlotWorkload[slot] != gpuWorkload) {
        PreparePass(gpuWorkload);
        slot = mWriteSlot;
        if (!mSlotRecorded[slot]) {
            return false;
        }
    }

    VkFence fence = mFences[slot].Get();
    vkResetFences(mDevice, 1, &fence);

    VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &mCommandBuffers[slot],
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr,
    };

    const int64_t submit_ns = get_time_ns();
    VkResult res = vkQueueSubmit(mContext.GetGraphicsQueue(), 1, &submitInfo, fence);
    if (res != VK_SUCCESS) {
        ALOGE("vkQueueSubmit failed: %d", res);
        mSlotRecorded[slot] = false;
        mSlotSubmitted[slot] = false;
        return false;
    }

    mSlotSubmitTimeNs[slot] = submit_ns;
    mSlotSubmitted[slot] = true;
    mSlotRecorded[slot] = false;
    mActiveSlot = slot;
    mWriteSlot = 1 - slot;
    return true;
}

samples::GpuPassTiming VulkanGpuCalibrationTarget::WaitPass() {
    if (mActiveSlot < 0 || !mSlotSubmitted[mActiveSlot]) {
        return {-1};
    }

    const int slot = mActiveSlot;
    VkFence fence = mFences[slot].Get();
    VkResult res = vkWaitForFences(mDevice, 1, &fence, VK_TRUE, kGpuPassTimeoutNs);
    const int64_t wall_ns = get_time_ns() - mSlotSubmitTimeNs[slot];
    if (res != VK_SUCCESS) {
        ALOGW("vkWaitForFences timed out or failed: %d", res);
        return {-1};
    }

    vkResetFences(mDevice, 1, &fence);
    mSlotSubmitted[slot] = false;
    mActiveSlot = -1;
    return {wall_ns};
}

samples::GpuPassTiming VulkanGpuCalibrationTarget::RunPass(int32_t gpuWorkload) {
    PreparePass(gpuWorkload);
    if (!SubmitPass(gpuWorkload)) {
        return {-1};
    }
    return WaitPass();
}

} // namespace samples::vulkan
