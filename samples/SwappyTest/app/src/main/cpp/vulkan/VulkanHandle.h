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

#include <cassert>
#include <utility>

namespace samples::vulkan {

/**
 * Generic move-only RAII wrapper for Vulkan handles destroyed via a parent handle
 * (typically VkDevice) and a deleter functor.
 */
template <typename HandleT, typename DeleterT, typename ParentT = VkDevice>
class VulkanHandle {
public:
    VulkanHandle() = default;

    VulkanHandle(ParentT parent, HandleT handle) noexcept : mParent(parent), mHandle(handle) {
        assert(mHandle == VK_NULL_HANDLE || mParent != VK_NULL_HANDLE);
    }

    ~VulkanHandle() noexcept {
        Reset();
    }

    VulkanHandle(const VulkanHandle&) = delete;
    VulkanHandle& operator=(const VulkanHandle&) = delete;

    VulkanHandle(VulkanHandle&& other) noexcept
          : mParent(std::exchange(other.mParent, ParentT{VK_NULL_HANDLE})),
            mHandle(std::exchange(other.mHandle, HandleT{VK_NULL_HANDLE})) {}

    VulkanHandle& operator=(VulkanHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            mParent = std::exchange(other.mParent, ParentT{VK_NULL_HANDLE});
            mHandle = std::exchange(other.mHandle, HandleT{VK_NULL_HANDLE});
        }
        return *this;
    }

    void Reset(ParentT parent = VK_NULL_HANDLE, HandleT handle = VK_NULL_HANDLE) noexcept {
        assert(handle == VK_NULL_HANDLE || parent != VK_NULL_HANDLE);
        if (mHandle != VK_NULL_HANDLE && mParent != VK_NULL_HANDLE) {
            DeleterT{}(mParent, mHandle);
        }
        mParent = parent;
        mHandle = handle;
    }

    HandleT* InitializeInto(ParentT parent) noexcept {
        assert(parent != VK_NULL_HANDLE);
        Reset();
        mParent = parent;
        return &mHandle;
    }

    [[nodiscard]] HandleT Get() const noexcept {
        return mHandle;
    }
    [[nodiscard]] bool IsValid() const noexcept {
        return mHandle != VK_NULL_HANDLE;
    }
    explicit operator bool() const noexcept {
        return IsValid();
    }

private:
    ParentT mParent = VK_NULL_HANDLE;
    HandleT mHandle = VK_NULL_HANDLE;
};

#define DEFINE_VULKAN_DEVICE_HANDLE(AliasName, VkType, DestroyFunc)      \
    struct AliasName##Deleter {                                          \
        void operator()(VkDevice device, VkType handle) const noexcept { \
            DestroyFunc(device, handle, nullptr);                        \
        }                                                                \
    };                                                                   \
    using AliasName = VulkanHandle<VkType, AliasName##Deleter, VkDevice>

DEFINE_VULKAN_DEVICE_HANDLE(ScopedShaderModule, VkShaderModule, vkDestroyShaderModule);
DEFINE_VULKAN_DEVICE_HANDLE(ScopedPipelineLayout, VkPipelineLayout, vkDestroyPipelineLayout);
DEFINE_VULKAN_DEVICE_HANDLE(ScopedPipeline, VkPipeline, vkDestroyPipeline);
DEFINE_VULKAN_DEVICE_HANDLE(ScopedRenderPass, VkRenderPass, vkDestroyRenderPass);
DEFINE_VULKAN_DEVICE_HANDLE(ScopedFramebuffer, VkFramebuffer, vkDestroyFramebuffer);
DEFINE_VULKAN_DEVICE_HANDLE(ScopedBuffer, VkBuffer, vkDestroyBuffer);
DEFINE_VULKAN_DEVICE_HANDLE(ScopedDeviceMemory, VkDeviceMemory, vkFreeMemory);
DEFINE_VULKAN_DEVICE_HANDLE(ScopedImageView, VkImageView, vkDestroyImageView);
DEFINE_VULKAN_DEVICE_HANDLE(ScopedSemaphore, VkSemaphore, vkDestroySemaphore);
DEFINE_VULKAN_DEVICE_HANDLE(ScopedFence, VkFence, vkDestroyFence);
DEFINE_VULKAN_DEVICE_HANDLE(ScopedImage, VkImage, vkDestroyImage);
DEFINE_VULKAN_DEVICE_HANDLE(ScopedSampler, VkSampler, vkDestroySampler);
DEFINE_VULKAN_DEVICE_HANDLE(ScopedDescriptorPool, VkDescriptorPool, vkDestroyDescriptorPool);
DEFINE_VULKAN_DEVICE_HANDLE(ScopedDescriptorSetLayout, VkDescriptorSetLayout,
                            vkDestroyDescriptorSetLayout);

#undef DEFINE_VULKAN_DEVICE_HANDLE

} // namespace samples::vulkan
