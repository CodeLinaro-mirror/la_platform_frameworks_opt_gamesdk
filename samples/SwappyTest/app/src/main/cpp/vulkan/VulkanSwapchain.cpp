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

#include "VulkanSwapchain.h"

#include <algorithm>
#include <cassert>

#include "VulkanContext.h"
#include "swappy/swappyVk.h"

namespace samples::vulkan {

VulkanSwapchain::~VulkanSwapchain() {
    Destroy();
}

bool VulkanSwapchain::InitializeSurface(VulkanContext* context, ANativeWindow* window) {
    assert(context != nullptr && context->GetInstance() != VK_NULL_HANDLE);
    assert(window != nullptr);

    Destroy();
    mContext = context;
    mWindow = window;
    ANativeWindow_acquire(mWindow);

    VkAndroidSurfaceCreateInfoKHR createInfo{
            .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .window = mWindow,
    };

    const VkResult result =
            vkCreateAndroidSurfaceKHR(mContext->GetInstance(), &createInfo, nullptr, &mSurface);
    if (result != VK_SUCCESS) {
        VK_LOGE("vkCreateAndroidSurfaceKHR failed: %s (%d)", VkResultToString(result),
                static_cast<int>(result));
        ANativeWindow_release(mWindow);
        mWindow = nullptr;
        return false;
    }
    return true;
}

VkSurfaceFormatKHR VulkanSwapchain::ChooseSurfaceFormat(
        std::span<const VkSurfaceFormatKHR> availableFormats) const noexcept {
    assert(!availableFormats.empty());
    for (const auto& format : availableFormats) {
        if (format.format == VK_FORMAT_R8G8B8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    for (const auto& format : availableFormats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return availableFormats[0];
}

bool VulkanSwapchain::CreateSwapchain(uint32_t width, uint32_t height) {
    assert(mContext != nullptr && mContext->IsInitialized());
    assert(mSurface != VK_NULL_HANDLE);
    if (width == 0 || height == 0) {
        VK_LOGW("Cannot create swapchain with zero dimensions (%ux%u)", width, height);
        return false;
    }

    VkPhysicalDevice physicalDevice = mContext->GetPhysicalDevice();
    VkDevice device = mContext->GetDevice();

    VkSurfaceCapabilitiesKHR capabilities{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, mSurface, &capabilities));

    uint32_t formatCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, mSurface, &formatCount, nullptr));
    if (formatCount == 0) {
        VK_LOGE("Physical device reported 0 surface formats");
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, mSurface, &formatCount,
                                                  formats.data()));
    formats.resize(formatCount);

    const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);
    mImageFormat = surfaceFormat.format;
    mColorSpace = surfaceFormat.colorSpace;

    if (capabilities.currentExtent.width != UINT32_MAX) {
        mExtent = capabilities.currentExtent;
    } else {
        mExtent.width = std::clamp(width, capabilities.minImageExtent.width,
                                   capabilities.maxImageExtent.width);
        mExtent.height = std::clamp(height, capabilities.minImageExtent.height,
                                    capabilities.maxImageExtent.height);
    }

    uint32_t imageCount = std::max(capabilities.minImageCount, 3u);
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    if (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
        compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }

    DestroySwapchain();

    VkSwapchainCreateInfoKHR createInfo{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .surface = mSurface,
            .minImageCount = imageCount,
            .imageFormat = mImageFormat,
            .imageColorSpace = mColorSpace,
            .imageExtent = mExtent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .preTransform = capabilities.currentTransform,
            .compositeAlpha = compositeAlpha,
            .presentMode = VK_PRESENT_MODE_FIFO_KHR,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE,
    };

    const VkResult result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &mSwapchain);
    if (result != VK_SUCCESS) {
        VK_LOGE("vkCreateSwapchainKHR failed: %s (%d)", VkResultToString(result),
                static_cast<int>(result));
        return false;
    }

    uint32_t actualImageCount = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(device, mSwapchain, &actualImageCount, nullptr));
    mImages.resize(actualImageCount);
    VK_CHECK(vkGetSwapchainImagesKHR(device, mSwapchain, &actualImageCount, mImages.data()));
    mImages.resize(actualImageCount);

    if (!CreateImageViews()) {
        DestroySwapchain();
        return false;
    }

    VK_LOGI("VulkanSwapchain created: %ux%u, format=%d, images=%u", mExtent.width, mExtent.height,
            static_cast<int>(mImageFormat), actualImageCount);
    return true;
}

bool VulkanSwapchain::CreateImageViews() {
    assert(mContext != nullptr && mContext->GetDevice() != VK_NULL_HANDLE);
    VkDevice device = mContext->GetDevice();

    mImageViews.clear();
    mImageViews.resize(mImages.size());

    for (size_t i = 0; i < mImages.size(); ++i) {
        VkImageViewCreateInfo viewInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .image = mImages[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = mImageFormat,
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

        const VkResult res = vkCreateImageView(device, &viewInfo, nullptr,
                                               mImageViews[i].InitializeInto(device));
        if (res != VK_SUCCESS) {
            VK_LOGE("vkCreateImageView failed for swapchain image %zu: %s (%d)", i,
                    VkResultToString(res), static_cast<int>(res));
            return false;
        }
    }
    return true;
}

VkResult VulkanSwapchain::AcquireNextImage(VkSemaphore imageAvailableSemaphore,
                                           uint32_t* outImageIndex) {
    assert(mContext != nullptr && mSwapchain != VK_NULL_HANDLE && outImageIndex != nullptr);
    return vkAcquireNextImageKHR(mContext->GetDevice(), mSwapchain, UINT64_MAX,
                                 imageAvailableSemaphore, VK_NULL_HANDLE, outImageIndex);
}

VkResult VulkanSwapchain::Present(VkQueue presentQueue, uint32_t imageIndex,
                                  VkSemaphore renderFinishedSemaphore, bool useSwappy) {
    assert(mSwapchain != VK_NULL_HANDLE);

    VkPresentInfoKHR presentInfo{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderFinishedSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &mSwapchain,
            .pImageIndices = &imageIndex,
            .pResults = nullptr,
    };

    if (useSwappy) {
        return SwappyVk_queuePresent(presentQueue, &presentInfo);
    }
    return vkQueuePresentKHR(presentQueue, &presentInfo);
}

void VulkanSwapchain::DestroySwapchain() {
    mImageViews.clear();
    mImages.clear();

    if (mSwapchain != VK_NULL_HANDLE && mContext != nullptr &&
        mContext->GetDevice() != VK_NULL_HANDLE) {
        SwappyVk_destroySwapchain(mContext->GetDevice(), mSwapchain);
        vkDestroySwapchainKHR(mContext->GetDevice(), mSwapchain, nullptr);
        mSwapchain = VK_NULL_HANDLE;
    }
}

void VulkanSwapchain::DestroySurface() {
    if (mSurface != VK_NULL_HANDLE && mContext != nullptr &&
        mContext->GetInstance() != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(mContext->GetInstance(), mSurface, nullptr);
        mSurface = VK_NULL_HANDLE;
    }
    if (mWindow != nullptr) {
        ANativeWindow_release(mWindow);
        mWindow = nullptr;
    }
}

void VulkanSwapchain::Destroy() {
    if (mContext != nullptr && mContext->IsInitialized()) {
        mContext->WaitIdle();
    }
    DestroySwapchain();
    DestroySurface();
    mContext = nullptr;
}

} // namespace samples::vulkan
