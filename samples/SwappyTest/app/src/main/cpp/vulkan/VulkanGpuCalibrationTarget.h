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

#include <cstdint>
#include <vector>

#include "../Circle.h"
#include "../GpuCalibrationTarget.h"
#include "VulkanHandle.h"

namespace samples::vulkan {

class VulkanContext;
class VulkanCircleRenderer;

/**
 * Offscreen Vulkan target for Swappy-independent GPU calibration.
 * Allocates a dedicated DEVICE_LOCAL VkImage (swapchain format & extent,
 * COLOR_ATTACHMENT_OPTIMAL layout) and renders via VulkanCircleRenderer::RecordOffscreenDraw,
 * measuring pass wall time via VkFence and vkWaitForFences.
 */
class VulkanGpuCalibrationTarget : public samples::GpuCalibrationTarget {
public:
    VulkanGpuCalibrationTarget(VulkanContext& context, const VulkanCircleRenderer& circleRenderer,
                               VkFormat colorFormat, VkExtent2D extent, float aspectRatio,
                               std::vector<samples::Circle> circles);

    ~VulkanGpuCalibrationTarget() override;

    VulkanGpuCalibrationTarget(const VulkanGpuCalibrationTarget&) = delete;
    VulkanGpuCalibrationTarget& operator=(const VulkanGpuCalibrationTarget&) = delete;

    [[nodiscard]] bool IsValid() const noexcept {
        return mFramebuffer.IsValid() && mFences[kNumSlots - 1].IsValid();
    }

    void PreparePass(int32_t gpuWorkload) override;
    bool SubmitPass(int32_t gpuWorkload) override;
    samples::GpuPassTiming WaitPass() override;
    samples::GpuPassTiming RunPass(int32_t gpuWorkload) override;
    const char* WallTimerName() const override {
        return "vk_fence";
    }
    int64_t PixelCount() const override {
        return static_cast<int64_t>(mExtent.width) * mExtent.height;
    }

private:
    static constexpr int kNumSlots = 2;

    VulkanContext& mContext;
    const VulkanCircleRenderer& mCircleRenderer;
    VkExtent2D mExtent{0, 0};
    float mAspectRatio = 1.0f;
    std::vector<samples::Circle> mCircles;

    VkDevice mDevice = VK_NULL_HANDLE;
    ScopedDeviceMemory mImageMemory;
    ScopedImage mImage;
    ScopedImageView mImageView;
    ScopedRenderPass mRenderPass;
    ScopedFramebuffer mFramebuffer;

    VkCommandBuffer mCommandBuffers[kNumSlots] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    ScopedFence mFences[kNumSlots];
    bool mSlotRecorded[kNumSlots] = {false, false};
    bool mSlotSubmitted[kNumSlots] = {false, false};
    int32_t mSlotWorkload[kNumSlots] = {0, 0};
    int64_t mSlotSubmitTimeNs[kNumSlots] = {0, 0};
    int mWriteSlot = 0;
    int mActiveSlot = -1;
};

} // namespace samples::vulkan
