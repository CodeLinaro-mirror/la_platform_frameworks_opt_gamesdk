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

#include "VulkanContext.h"
#include "VulkanHandle.h"
#include "VulkanUtils.h"

namespace samples::vulkan {

/**
 * Manages a VkBuffer and its bound host-visible VkDeviceMemory.
 */
class VulkanBuffer {
public:
    VulkanBuffer() = default;
    ~VulkanBuffer() = default;

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&&) noexcept = default;
    VulkanBuffer& operator=(VulkanBuffer&&) noexcept = default;

    [[nodiscard]] static VulkanBuffer CreateVertexBuffer(VulkanContext& context,
                                                         const void* vertexData, VkDeviceSize size);

    [[nodiscard]] bool Create(VulkanContext* context, VkDeviceSize size, VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags properties);

    bool UploadData(const void* data, VkDeviceSize size);

    void Destroy() noexcept {
        Reset();
    }

    void Reset() noexcept {
        mBuffer.Reset();
        mMemory = VK_NULL_HANDLE;
        mMappedPtr = nullptr;
        mOffset = 0;
        mSize = 0;
    }

    [[nodiscard]] VkBuffer GetBuffer() const noexcept {
        return mBuffer.Get();
    }
    [[nodiscard]] bool IsValid() const noexcept {
        return mBuffer.IsValid() && mMemory != VK_NULL_HANDLE;
    }

private:
    ScopedBuffer mBuffer;
    VkDeviceMemory mMemory = VK_NULL_HANDLE;
    void* mMappedPtr = nullptr;
    VkDeviceSize mOffset = 0;
    VkDeviceSize mSize = 0;
};

} // namespace samples::vulkan
