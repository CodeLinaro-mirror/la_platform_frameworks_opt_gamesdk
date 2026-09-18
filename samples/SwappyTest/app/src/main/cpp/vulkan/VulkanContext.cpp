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

#include "VulkanContext.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

#include "swappy/swappyVk.h"

namespace samples::vulkan {

namespace {

constexpr const char* kRequiredInstanceExtensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
};

bool HasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name) noexcept {
    for (const auto& ext : extensions) {
        if (std::strcmp(ext.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

VulkanContext::~VulkanContext() {
    Destroy();
}

bool VulkanContext::InitializeInstance() {
    if (mInstance != VK_NULL_HANDLE) {
        return true;
    }

    uint32_t extCount = 0;
    VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr));
    std::vector<VkExtensionProperties> availableExts(extCount);
    if (extCount > 0) {
        VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &extCount, availableExts.data()));
        availableExts.resize(extCount);
    }

    for (const char* req : kRequiredInstanceExtensions) {
        if (!HasExtension(availableExts, req)) {
            VK_LOGE("Missing required Vulkan instance extension: %s", req);
            return false;
        }
    }

    VkApplicationInfo applicationInfo{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "SwappyTest",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "SwappyTestVulkan",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = VK_API_VERSION_1_1,
    };

    VkInstanceCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pApplicationInfo = &applicationInfo,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = static_cast<uint32_t>(std::size(kRequiredInstanceExtensions)),
            .ppEnabledExtensionNames = kRequiredInstanceExtensions,
    };

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &mInstance);
    if (result != VK_SUCCESS) {
        VK_LOGE("vkCreateInstance failed: %s (%d)", VkResultToString(result),
                static_cast<int>(result));
        return false;
    }
    return true;
}

bool VulkanContext::FindQueueFamilies(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                      uint32_t& outQueueFamilyIndex) const {
    assert(surface != VK_NULL_HANDLE);
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            VkBool32 presentSupport = VK_FALSE;
            VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface,
                                                          &presentSupport));
            if (presentSupport == VK_TRUE) {
                outQueueFamilyIndex = i;
                return true;
            }
        }
    }
    return false;
}

bool VulkanContext::PickPhysicalDevice(VkSurfaceKHR surface) {
    assert(surface != VK_NULL_HANDLE);
    uint32_t deviceCount = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(mInstance, &deviceCount, nullptr));
    if (deviceCount == 0) {
        VK_LOGE("No Vulkan physical devices found");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    VK_CHECK(vkEnumeratePhysicalDevices(mInstance, &deviceCount, devices.data()));
    devices.resize(deviceCount);

    for (VkPhysicalDevice physicalDevice : devices) {
        uint32_t queueFamilyIndex = UINT32_MAX;
        if (!FindQueueFamilies(physicalDevice, surface, queueFamilyIndex)) {
            continue;
        }

        uint32_t extCount = 0;
        VK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr));
        std::vector<VkExtensionProperties> availableExts(extCount);
        if (extCount > 0) {
            VK_CHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount,
                                                          availableExts.data()));
            availableExts.resize(extCount);
        }

        if (!HasExtension(availableExts, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }

        mPhysicalDevice = physicalDevice;
        mGraphicsQueueFamily = queueFamilyIndex;
        break;
    }

    if (mPhysicalDevice == VK_NULL_HANDLE) {
        VK_LOGE("Failed to find suitable Vulkan physical device");
        return false;
    }

    vkGetPhysicalDeviceProperties(mPhysicalDevice, &mPhysicalDeviceProperties);
    vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &mMemoryProperties);

    VK_LOGI("Selected Vulkan physical device: %s (API %u.%u.%u, driver 0x%x)",
            mPhysicalDeviceProperties.deviceName,
            VK_VERSION_MAJOR(mPhysicalDeviceProperties.apiVersion),
            VK_VERSION_MINOR(mPhysicalDeviceProperties.apiVersion),
            VK_VERSION_PATCH(mPhysicalDeviceProperties.apiVersion),
            mPhysicalDeviceProperties.driverVersion);
    return true;
}

bool VulkanContext::InitializeDevice(VkSurfaceKHR surface) {
    assert(mInstance != VK_NULL_HANDLE);
    assert(surface != VK_NULL_HANDLE);
    if (mDevice != VK_NULL_HANDLE) {
        return true;
    }

    if (mPhysicalDevice == VK_NULL_HANDLE && !PickPhysicalDevice(surface)) {
        return false;
    }

    uint32_t availCount = 0;
    VK_CHECK(vkEnumerateDeviceExtensionProperties(mPhysicalDevice, nullptr, &availCount, nullptr));
    std::vector<VkExtensionProperties> availableExts(availCount);
    if (availCount > 0) {
        VK_CHECK(vkEnumerateDeviceExtensionProperties(mPhysicalDevice, nullptr, &availCount,
                                                      availableExts.data()));
        availableExts.resize(availCount);
    }

    uint32_t swappyReqCount = 0;
    SwappyVk_determineDeviceExtensions(mPhysicalDevice, availCount, availableExts.data(),
                                       &swappyReqCount, nullptr);
    std::vector<std::array<char, VK_MAX_EXTENSION_NAME_SIZE>> swappyExtStorage(swappyReqCount);
    std::vector<char*> swappyExtPtrs(swappyReqCount);
    for (uint32_t i = 0; i < swappyReqCount; ++i) {
        swappyExtStorage[i].fill('\0');
        swappyExtPtrs[i] = swappyExtStorage[i].data();
    }
    if (swappyReqCount > 0) {
        SwappyVk_determineDeviceExtensions(mPhysicalDevice, availCount, availableExts.data(),
                                           &swappyReqCount, swappyExtPtrs.data());
    }

    std::vector<const char*> enabledExtensions;
    enabledExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    for (uint32_t i = 0; i < swappyReqCount; ++i) {
        const char* extName = swappyExtPtrs[i];
        if (extName[0] == '\0') continue;
        bool duplicate = false;
        for (const char* existing : enabledExtensions) {
            if (std::strcmp(existing, extName) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            enabledExtensions.push_back(extName);
            VK_LOGI("Enabling SwappyVk device extension: %s", extName);
        }
    }

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = mGraphicsQueueFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
    };

    VkPhysicalDeviceFeatures deviceFeatures{};
    VkDeviceCreateInfo deviceCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCreateInfo,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size()),
            .ppEnabledExtensionNames = enabledExtensions.data(),
            .pEnabledFeatures = &deviceFeatures,
    };

    const VkResult result = vkCreateDevice(mPhysicalDevice, &deviceCreateInfo, nullptr, &mDevice);
    if (result != VK_SUCCESS) {
        VK_LOGE("vkCreateDevice failed: %s (%d)", VkResultToString(result),
                static_cast<int>(result));
        return false;
    }

    vkGetDeviceQueue(mDevice, mGraphicsQueueFamily, 0, &mGraphicsQueue);
    SwappyVk_setQueueFamilyIndex(mDevice, mGraphicsQueue, mGraphicsQueueFamily);

    VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = mGraphicsQueueFamily,
    };
    VK_CHECK(vkCreateCommandPool(mDevice, &poolInfo, nullptr, &mCommandPool));
    return true;
}

void VulkanContext::Destroy() {
    WaitIdle();

    if (mCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(mDevice, mCommandPool, nullptr);
        mCommandPool = VK_NULL_HANDLE;
    }

    if (mDevice != VK_NULL_HANDLE) {
        for (auto& arena : mMemoryArenas) {
            if (arena.memory != VK_NULL_HANDLE) {
                if (arena.mappedBase != nullptr) {
                    vkUnmapMemory(mDevice, arena.memory);
                }
                vkFreeMemory(mDevice, arena.memory, nullptr);
                arena = {};
            }
        }
        SwappyVk_destroyDevice(mDevice);
        vkDestroyDevice(mDevice, nullptr);
        mDevice = VK_NULL_HANDLE;
    }

    if (mInstance != VK_NULL_HANDLE) {
        vkDestroyInstance(mInstance, nullptr);
        mInstance = VK_NULL_HANDLE;
    }

    mPhysicalDevice = VK_NULL_HANDLE;
    mGraphicsQueue = VK_NULL_HANDLE;
    mGraphicsQueueFamily = UINT32_MAX;
}

void VulkanContext::WaitIdle() const {
    if (mDevice != VK_NULL_HANDLE) {
        VK_CHECK(vkDeviceWaitIdle(mDevice));
    }
}

std::optional<uint32_t> VulkanContext::FindMemoryType(
        uint32_t typeFilter, VkMemoryPropertyFlags properties) const noexcept {
    assert(mPhysicalDevice != VK_NULL_HANDLE);
    for (uint32_t i = 0; i < mMemoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (mMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    VK_LOGE("Failed to find suitable Vulkan memory type (filter=0x%x, props=0x%x)", typeFilter,
            properties);
    return std::nullopt;
}

bool VulkanContext::AllocateMemory(const VkMemoryRequirements& requirements,
                                   VkMemoryPropertyFlags properties, VkDeviceMemory* outMemory,
                                   VkDeviceSize* outOffset, void** outMappedPtr) {
    assert(mDevice != VK_NULL_HANDLE);
    assert(outMemory != nullptr && outOffset != nullptr);

    const auto memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, properties);
    if (!memoryTypeIndex) {
        return false;
    }

    const uint32_t typeIdx = *memoryTypeIndex;
    MemoryArena& arena = mMemoryArenas[typeIdx];
    const VkDeviceSize granularity = std::max<VkDeviceSize>(
            {requirements.alignment, mPhysicalDeviceProperties.limits.bufferImageGranularity,
             mPhysicalDeviceProperties.limits.nonCoherentAtomSize, VkDeviceSize{4096}});

    if (arena.memory == VK_NULL_HANDLE) {
        constexpr VkDeviceSize kDefaultArenaBytes = 8 * 1024 * 1024;
        const VkDeviceSize arenaBytes =
                std::max<VkDeviceSize>(kDefaultArenaBytes, requirements.size * 2);
        VkMemoryAllocateInfo allocInfo{
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .pNext = nullptr,
                .allocationSize = arenaBytes,
                .memoryTypeIndex = typeIdx,
        };
        const VkResult res = vkAllocateMemory(mDevice, &allocInfo, nullptr, &arena.memory);
        if (res != VK_SUCCESS) {
            VK_LOGE("vkAllocateMemory failed: %s (%d)", VkResultToString(res),
                    static_cast<int>(res));
            arena.memory = VK_NULL_HANDLE;
            return false;
        }
        arena.capacity = arenaBytes;
        arena.nextOffset = 0;

        const VkMemoryPropertyFlags typeFlags =
                mMemoryProperties.memoryTypes[typeIdx].propertyFlags;
        if ((typeFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
            void* mapped = nullptr;
            const VkResult mapRes =
                    vkMapMemory(mDevice, arena.memory, 0, arena.capacity, 0, &mapped);
            if (mapRes != VK_SUCCESS || mapped == nullptr) {
                VK_LOGE("vkMapMemory failed: %s (%d)", VkResultToString(mapRes),
                        static_cast<int>(mapRes));
                vkFreeMemory(mDevice, arena.memory, nullptr);
                arena = {};
                return false;
            }
            arena.mappedBase = static_cast<uint8_t*>(mapped);
        }
    }

    const VkDeviceSize alignedOffset = (arena.nextOffset + granularity - 1) & ~(granularity - 1);
    if (alignedOffset + requirements.size > arena.capacity) {
        VK_LOGE("Vulkan memory arena exhausted (used=%llu, req=%llu, cap=%llu)",
                static_cast<unsigned long long>(alignedOffset),
                static_cast<unsigned long long>(requirements.size),
                static_cast<unsigned long long>(arena.capacity));
        return false;
    }

    *outMemory = arena.memory;
    *outOffset = alignedOffset;
    if (outMappedPtr != nullptr) {
        *outMappedPtr =
                (arena.mappedBase != nullptr) ? (arena.mappedBase + alignedOffset) : nullptr;
    }
    arena.nextOffset = alignedOffset + requirements.size;
    return true;
}

VkCommandBuffer VulkanContext::CreateCommandBuffer(VkCommandBufferLevel level) {
    assert(mDevice != VK_NULL_HANDLE && mCommandPool != VK_NULL_HANDLE);
    VkCommandBufferAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = mCommandPool,
            .level = level,
            .commandBufferCount = 1,
    };
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(mDevice, &allocateInfo, &commandBuffer));
    return commandBuffer;
}

void VulkanContext::FreeCommandBuffer(VkCommandBuffer commandBuffer) {
    assert(mDevice != VK_NULL_HANDLE && mCommandPool != VK_NULL_HANDLE);
    vkFreeCommandBuffers(mDevice, mCommandPool, 1, &commandBuffer);
}

} // namespace samples::vulkan
