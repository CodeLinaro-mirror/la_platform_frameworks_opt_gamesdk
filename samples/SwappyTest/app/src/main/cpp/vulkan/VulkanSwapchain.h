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

#include <android/native_window.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <cstdint>
#include <span>
#include <vector>

#include "VulkanHandle.h"
#include "VulkanUtils.h"

namespace samples::vulkan {

class VulkanContext;

/**
 * Manages the Android VkSurfaceKHR, VkSwapchainKHR, swapchain images, and image views,
 * routing presentation through SwappyVk_queuePresent or vkQueuePresentKHR.
 */
class VulkanSwapchain {
public:
    VulkanSwapchain() = default;
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
    VulkanSwapchain(VulkanSwapchain&&) = delete;
    VulkanSwapchain& operator=(VulkanSwapchain&&) = delete;

    [[nodiscard]] bool InitializeSurface(VulkanContext* context, ANativeWindow* window);
    [[nodiscard]] bool CreateSwapchain(uint32_t width, uint32_t height);
    void DestroySwapchain();
    void DestroySurface();
    void Destroy();

    [[nodiscard]] VkResult AcquireNextImage(VkSemaphore imageAvailableSemaphore,
                                            uint32_t* outImageIndex);

    [[nodiscard]] VkResult Present(VkQueue presentQueue, uint32_t imageIndex,
                                   VkSemaphore renderFinishedSemaphore, bool useSwappy);

    [[nodiscard]] VkSurfaceKHR GetSurface() const noexcept {
        return mSurface;
    }
    [[nodiscard]] VkSwapchainKHR GetSwapchain() const noexcept {
        return mSwapchain;
    }
    [[nodiscard]] VkFormat GetImageFormat() const noexcept {
        return mImageFormat;
    }
    [[nodiscard]] VkExtent2D GetExtent() const noexcept {
        return mExtent;
    }
    [[nodiscard]] size_t GetImageCount() const noexcept {
        return mImages.size();
    }
    [[nodiscard]] std::span<const ScopedImageView> GetImageViews() const noexcept {
        return mImageViews;
    }
    [[nodiscard]] bool IsValid() const noexcept {
        return mSwapchain != VK_NULL_HANDLE;
    }

private:
    [[nodiscard]] bool CreateImageViews();
    [[nodiscard]] VkSurfaceFormatKHR ChooseSurfaceFormat(
            std::span<const VkSurfaceFormatKHR> availableFormats) const noexcept;

    VulkanContext* mContext = nullptr;
    ANativeWindow* mWindow = nullptr;
    VkSurfaceKHR mSurface = VK_NULL_HANDLE;
    VkSwapchainKHR mSwapchain = VK_NULL_HANDLE;

    VkFormat mImageFormat = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR mColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D mExtent = {0, 0};

    std::vector<VkImage> mImages;
    std::vector<ScopedImageView> mImageViews;
};

} // namespace samples::vulkan
