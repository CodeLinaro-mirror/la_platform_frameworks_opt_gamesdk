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

#include "VulkanBuffer.h"

#include <cassert>
#include <cstring>

namespace samples::vulkan {

VulkanBuffer VulkanBuffer::CreateVertexBuffer(VulkanContext& context, const void* vertexData,
                                              VkDeviceSize size) {
    VulkanBuffer result;
    const VkMemoryPropertyFlags flags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!result.Create(&context, size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, flags) ||
        !result.UploadData(vertexData, size)) {
        result.Destroy();
    }
    return result;
}

bool VulkanBuffer::Create(VulkanContext* context, VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties) {
    assert(context != nullptr);
    assert(context->IsInitialized());
    assert(size > 0);
    Reset();
    constexpr VkMemoryPropertyFlags kRequiredHostFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if ((properties & kRequiredHostFlags) != kRequiredHostFlags) {
        return false;
    }

    VkDevice device = context->GetDevice();
    VkBufferCreateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
    };

    VkResult res = vkCreateBuffer(device, &bufferInfo, nullptr, mBuffer.InitializeInto(device));
    if (res != VK_SUCCESS) {
        Reset();
        return false;
    }

    VkMemoryRequirements memRequirements{};
    vkGetBufferMemoryRequirements(device, mBuffer.Get(), &memRequirements);

    if (!context->AllocateMemory(memRequirements, properties, &mMemory, &mOffset, &mMappedPtr)) {
        Reset();
        return false;
    }

    res = vkBindBufferMemory(device, mBuffer.Get(), mMemory, mOffset);
    if (res != VK_SUCCESS) {
        Reset();
        return false;
    }

    mSize = size;
    return true;
}

bool VulkanBuffer::UploadData(const void* data, VkDeviceSize size) {
    if (!IsValid() || mMappedPtr == nullptr || data == nullptr || size == 0 || size > mSize) {
        return false;
    }
    std::memcpy(mMappedPtr, data, static_cast<size_t>(size));
    return true;
}

} // namespace samples::vulkan
