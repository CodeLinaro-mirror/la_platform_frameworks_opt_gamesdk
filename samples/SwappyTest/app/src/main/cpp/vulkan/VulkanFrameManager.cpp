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

#include "VulkanFrameManager.h"

#include <cassert>

#include "VulkanContext.h"

namespace samples::vulkan {

VulkanFrameManager::~VulkanFrameManager() {
    Destroy();
}

void VulkanFrameManager::Initialize(VulkanContext* context, size_t framesInFlight,
                                    size_t swapchainImageCount) {
    assert(context != nullptr && context->IsInitialized());
    assert(framesInFlight > 0);
    assert(swapchainImageCount > 0);

    Destroy();
    mContext = context;
    VkDevice device = mContext->GetDevice();

    mFrames.resize(framesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
    };

    VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (size_t i = 0; i < framesInFlight; ++i) {
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                                   mFrames[i].imageAvailableSemaphore.InitializeInto(device)));
        VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr,
                               mFrames[i].inFlightFence.InitializeInto(device)));
        mFrames[i].commandBuffer = mContext->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    }

    EnsureRenderFinishedSemaphores(swapchainImageCount);
    mCurrentFrame = 0;
}

void VulkanFrameManager::EnsureRenderFinishedSemaphores(size_t count) {
    assert(mContext != nullptr && mContext->GetDevice() != VK_NULL_HANDLE);
    if (mRenderFinishedSemaphores.size() >= count) {
        return;
    }

    VkDevice device = mContext->GetDevice();
    VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
    };

    const size_t oldSize = mRenderFinishedSemaphores.size();
    mRenderFinishedSemaphores.resize(count);
    for (size_t i = oldSize; i < count; ++i) {
        VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                                   mRenderFinishedSemaphores[i].InitializeInto(device)));
    }
}

VkCommandBuffer VulkanFrameManager::BeginFrame(VulkanSwapchain* swapchain,
                                               uint32_t* outImageIndex) {
    assert(mContext != nullptr && swapchain != nullptr && swapchain->IsValid() &&
           outImageIndex != nullptr);

    VkDevice device = mContext->GetDevice();
    FrameResources& frame = mFrames[mCurrentFrame];

    const VkFence fence = frame.inFlightFence.Get();
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));

    const VkResult acquireResult =
            swapchain->AcquireNextImage(frame.imageAvailableSemaphore.Get(), outImageIndex);
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            VK_LOGW("Swapchain out of date during vkAcquireNextImageKHR");
        } else {
            VK_LOGE("Error during vkAcquireNextImageKHR: %s (%d)", VkResultToString(acquireResult),
                    static_cast<int>(acquireResult));
        }
        return VK_NULL_HANDLE;
    }

    mSuboptimalAcquire = (acquireResult == VK_SUBOPTIMAL_KHR);

    // Reset fence only after image acquisition succeeds so we never deadlock on early return.
    VK_CHECK(vkResetFences(device, 1, &fence));

    VK_CHECK(vkResetCommandBuffer(frame.commandBuffer, 0));
    VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
    };
    VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

    return frame.commandBuffer;
}

VkResult VulkanFrameManager::EndFrame(VulkanSwapchain* swapchain, uint32_t imageIndex,
                                      VkCommandBuffer commandBuffer, bool useSwappy) {
    assert(mContext != nullptr && swapchain != nullptr && commandBuffer != VK_NULL_HANDLE);

    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    FrameResources& frame = mFrames[mCurrentFrame];

    assert(imageIndex < mRenderFinishedSemaphores.size());
    const VkSemaphore renderFinishedSemaphore = mRenderFinishedSemaphores[imageIndex].Get();
    const VkSemaphore waitSemaphore = frame.imageAvailableSemaphore.Get();

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &waitSemaphore,
            .pWaitDstStageMask = &waitStage,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &renderFinishedSemaphore,
    };

    VK_CHECK(
            vkQueueSubmit(mContext->GetGraphicsQueue(), 1, &submitInfo, frame.inFlightFence.Get()));

    const VkResult presentResult = swapchain->Present(mContext->GetPresentQueue(), imageIndex,
                                                      renderFinishedSemaphore, useSwappy);

    const bool wasSuboptimalAcquire = mSuboptimalAcquire;
    mSuboptimalAcquire = false;

    mCurrentFrame = (mCurrentFrame + 1) % mFrames.size();

    if (wasSuboptimalAcquire && presentResult == VK_SUCCESS) {
        return VK_SUBOPTIMAL_KHR;
    }
    return presentResult;
}

void VulkanFrameManager::Destroy() {
    if (mContext != nullptr) {
        assert(mContext->GetDevice() != VK_NULL_HANDLE);
        mContext->WaitIdle();
        for (auto& frame : mFrames) {
            mContext->FreeCommandBuffer(frame.commandBuffer);
        }
        mContext = nullptr;
    }
    mFrames.clear();
    mRenderFinishedSemaphores.clear();
    mCurrentFrame = 0;
    mSuboptimalAcquire = false;
}

} // namespace samples::vulkan
