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

#include <android/log.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>
#define LOG_TAG "SWAPPY_MOCK_LAYER"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#define VK_LAYER_EXPORT __attribute__((visibility("default")))
#define LAYER_NAME "VK_LAYER_swappy_mock"

/* ==========================================================================================
 * Swappy Vulkan Mock Layer (VK_LAYER_swappy_mock) & Android Vulkan Loader Architecture Guide
 * ==========================================================================================
 *
 * 1. PURPOSE IN SWAPPY TESTS:
 *    - This layer emulates the `VK_GOOGLE_display_timing` Vulkan extension on devices and emulators
 *      that do not natively support it, or where deterministic mock timing is needed for unit
 * tests.
 *    - It intercepts `vkQueuePresentKHR`, `vkGetRefreshCycleDurationGOOGLE`, and
 *      `vkGetPastPresentationTimingGOOGLE`.
 *    - When `vkQueuePresentKHR` is called with `VkPresentTimeGOOGLE` attached to `pNext`, the layer
 *      records the presentation timing in a history buffer.
 *    - When Swappy's Vulkan frame-pacing backend (`SwappyVkGoogleDisplayTiming.cpp`) queries
 *      `vkGetPastPresentationTimingGOOGLE`, the layer returns the recorded presentation timings.
 *    - The layer's mock timing emulation is toggled via the `MOCK_VK_GOOGLE_DISPLAY_TIMING`
 * environment variable (1 = enabled, 0 = disabled), which is read once during `vkCreateInstance`
 * and cached in `g_mockTimingEnabled`.
 *    - In Swappy's instrumentation tests (`DisplayTimingMockEnabledTest` and
 *      `DisplayTimingMockDisabledTest` in `swappy_basic_test.cpp`), this layer allows testing the
 *      full `VK_GOOGLE_display_timing` backend path on any test emulator without hardware support.
 *
 * 2. ARCHITECTURE GUIDE FOR DEVELOPERS AND AI AGENTS WRITING ANDROID VULKAN LAYERS:
 *    Implementing a Vulkan layer on Android (`libvulkan.so`) requires handling several subtle
 * loader behaviors and dynamic linking rules that differ from standard desktop Vulkan loaders:
 *
 *    A. LAYER DISCOVERY & EXTENSION STRIPPING IN vkCreateDevice:
 *       - The Android loader discovers layers by calling `vkEnumerateInstanceLayerProperties`,
 *         `vkEnumerateDeviceLayerProperties`, and their extension enumeration counterparts.
 *       - When a layer advertises and implements an extension (`VK_GOOGLE_display_timing`), it MUST
 *         strip its own extension name from `pCreateInfo->ppEnabledExtensionNames` in
 * `vkCreateDevice` BEFORE forwarding `pCreateInfo` down the layer chain to `realCreateDevice`.
 * Otherwise, if the underlying driver does not support the extension, `realCreateDevice` will fail
 * with `VK_ERROR_EXTENSION_NOT_PRESENT`. Even if the driver does advertise it, passing it down can
 * cause the driver to claim the extension and conflict with the layer's interception.
 *
 *    B. ELF SYMBOL PREEMPTION & INFINITE RECURSION LOOPS (CRITICAL GOTCHA):
 *       - On Linux/Android, `libvulkan.so` exports `vkGetInstanceProcAddr` and
 * `vkGetDeviceProcAddr` in the global symbol namespace.
 *       - If a layer `.so` defines a function literally named `vkGetDeviceProcAddr` and returns
 *         `reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr)` for symbol queries, ELF
 * dynamic symbol interposition rules will resolve the address of `&vkGetDeviceProcAddr` to
 *         `libvulkan.so`'s exported symbol instead of the layer's own implementation!
 *       - When `libvulkan.so` stores that pointer in its device dispatch table
 * (`DeviceDriverTable`), any call to `vkGetDeviceProcAddr` enters an infinite recursion loop inside
 * `libvulkan.so`: `vkGetDeviceProcAddr` -> `GetData(device).dispatch.GetDeviceProcAddr` ->
 * `vkGetDeviceProcAddr` causing the application to hang or crash with a stack overflow.
 *       - HOW TO AVOID THIS: Implement the core layer logic in functions with unique,
 * non-preemptible symbol names (e.g., `SwappyMock_vkGetInstanceProcAddr` and
 * `SwappyMock_vkGetDeviceProcAddr`). Return the addresses of `SwappyMock_vk*` for all GPA string
 * queries, and ONLY export `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr` at the very bottom of
 * the file as thin forwarding wrappers around `SwappyMock_vk*`.
 *
 *    C. LOADER DISPATCH TABLE INITIALIZATION (`InitDispatchTable`) & g_lastNextGdpa FALLBACK:
 *       - During `vkCreateDevice`, `libvulkan.so` calls the layer's `vkCreateDevice`. The layer
 * calls down the chain (`realCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice)`).
 *       - IMMEDIATELY BEFORE `realCreateDevice` returns to the layer, `libvulkan.so` initializes
 * the new device's dispatch table (`driver::DeviceDriverTable`) by calling the layer's
 *         `vkGetDeviceProcAddr(device, pName)` for all 40 core Vulkan commands (`vkCreateBuffer`,
 *         `vkQueueSubmit`, `vkDestroyDevice`, etc.).
 *       - At that exact moment, the layer has not yet executed `g_deviceDispatch[*pDevice] =
 * nextGdpa;` because `realCreateDevice` has not yet returned!
 *       - If the layer's `vkGetDeviceProcAddr` simply looks up `device` in `g_deviceDispatch` and
 *         returns `nullptr` when not found, `libvulkan.so` will populate its entire dispatch table
 * with `nullptr` function pointers, causing the application to crash or freeze on its first Vulkan
 *         call.
 *       - HOW TO AVOID THIS: In `vkCreateDevice`, store the next layer/driver's GPA (`nextGdpa`) in
 * an atomic fallback variable (`g_lastNextGdpa`) BEFORE calling `realCreateDevice(...)`. In
 *         `vkGetDeviceProcAddr`, if `device` is not yet found in `g_deviceDispatch`, fall back to
 *         `g_lastNextGdpa.load()(device, pName)` instead of returning `nullptr`.
 *
 *    D. EXPORTING REQUIRED COMMANDS IN BOTH GPA FUNCTIONS:
 *       - The loader calls `vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr")` to retrieve the
 *         layer's device-level query function. A layer MUST intercept `"vkGetDeviceProcAddr"` in
 * both `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr` and return its own implementation, or else
 *         the loader will fall through to the driver's GPA and bypass the layer entirely for device
 *         functions.
 *       - The loader may also resolve device extension functions via `vkGetInstanceProcAddr` during
 *         device creation, so custom extension functions (`vkGetPastPresentationTimingGOOGLE` and
 *         `vkGetRefreshCycleDurationGOOGLE`) MUST be exposed in `vkGetInstanceProcAddr` as well as
 *         in `vkGetDeviceProcAddr`.
 * ==========================================================================================
 */

// Prevent infinite recursion loops:
// Calls to vkEnumerateInstanceLayerProperties, vkEnumerateDeviceLayerProperties,
// vkEnumerateInstanceExtensionProperties, and vkEnumerateDeviceExtensionProperties
// resolved by the dynamic loader (libvulkan.so) can route back into the layer due to ELF
struct InstanceContext {
    PFN_vkGetInstanceProcAddr nextGipa;
};
static std::unordered_map<VkInstance, InstanceContext> g_instanceContexts;
static std::unordered_map<VkPhysicalDevice, VkInstance> g_physDevToInstance;
static std::unordered_map<VkInstance, PFN_vkGetInstanceProcAddr> g_instanceDispatch;
static std::unordered_map<VkDevice, PFN_vkGetDeviceProcAddr> g_deviceDispatch;
static std::unordered_map<VkQueue, VkDevice> g_queueToDevice;
static std::atomic<bool> g_mockTimingEnabled{false};
static thread_local PFN_vkGetDeviceProcAddr g_lastNextGdpa{nullptr};
static std::mutex g_lock;

extern "C" VKAPI_ATTR VkResult VKAPI_CALL SwappyMock_vkEnumerateInstanceLayerProperties(
        uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
    LOGI("vk_layer_swappy_mock: SwappyMock_vkEnumerateInstanceLayerProperties");
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (pProperties == nullptr) {
        *pPropertyCount = 1;
        return VK_SUCCESS;
    }
    if (*pPropertyCount < 1) return VK_INCOMPLETE;
    VkLayerProperties layerProps{};
    std::strcpy(layerProps.layerName, LAYER_NAME);
    std::strcpy(layerProps.description, "Mock Layer for Swappy tests");
    layerProps.implementationVersion = 1;
    layerProps.specVersion = VK_API_VERSION_1_0;
    *pProperties = layerProps;
    *pPropertyCount = 1;
    return VK_SUCCESS;
}

extern "C" VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
    return SwappyMock_vkEnumerateInstanceLayerProperties(pPropertyCount, pProperties);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL SwappyMock_vkEnumerateDeviceLayerProperties(
        VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
    LOGI("vk_layer_swappy_mock: SwappyMock_vkEnumerateDeviceLayerProperties");
    if (!pPropertyCount) return VK_ERROR_INITIALIZATION_FAILED;
    if (pProperties == nullptr) {
        *pPropertyCount = 1;
        return VK_SUCCESS;
    }
    if (*pPropertyCount < 1) return VK_INCOMPLETE;
    VkLayerProperties layerProps{};
    std::strcpy(layerProps.layerName, LAYER_NAME);
    std::strcpy(layerProps.description, "Mock Layer for Swappy tests");
    layerProps.implementationVersion = 1;
    layerProps.specVersion = VK_API_VERSION_1_0;
    *pProperties = layerProps;
    *pPropertyCount = 1;
    return VK_SUCCESS;
}

extern "C" VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
        VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
    return SwappyMock_vkEnumerateDeviceLayerProperties(physicalDevice, pPropertyCount, pProperties);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL SwappyMock_vkEnumerateInstanceExtensionProperties(
        const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
    LOGI("vk_layer_swappy_mock: SwappyMock_vkEnumerateInstanceExtensionProperties "
         "(pLayerName=%s)",
         pLayerName ? pLayerName : "null");
    if (!pLayerName || (std::strcmp(pLayerName, LAYER_NAME) == 0)) {
        if (!pProperties) {
            *pPropertyCount = 0;
            return VK_SUCCESS;
        }
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }
    return VK_ERROR_LAYER_NOT_PRESENT;
}

extern "C" VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
        const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
    return SwappyMock_vkEnumerateInstanceExtensionProperties(pLayerName, pPropertyCount,
                                                             pProperties);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL SwappyMock_vkEnumerateDeviceExtensionProperties(
        VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount,
        VkExtensionProperties* pProperties) {
    LOGI("vk_layer_swappy_mock: SwappyMock_vkEnumerateDeviceExtensionProperties "
         "(pLayerName=%s)",
         pLayerName ? pLayerName : "null");

    // If querying for this specific layer
    if (pLayerName && (std::strcmp(pLayerName, LAYER_NAME) == 0)) {
        LOGI("vk_layer_swappy_mock: SwappyMock_vkEnumerateDeviceExtensionProperties "
             "layer %s properties query, returning %s",
             LAYER_NAME, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
        if (!pProperties) {
            *pPropertyCount = 1;
            return VK_SUCCESS;
        }
        if (*pPropertyCount < 1) return VK_INCOMPLETE;
        pProperties[0] = {VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME, VK_MAKE_VERSION(1, 0, 0)};
        *pPropertyCount = 1;
        return VK_SUCCESS;
    }

    // When pLayerName is NULL (querying all device extensions), we MUST NOT return ONLY our mock
    // extension. Doing so would make the loader believe the driver does not support standard
    // swapchain extensions (like VK_KHR_swapchain), causing vkCreateDevice to fail. Instead, we
    // query the driver's device extensions using the next layer proc addr (passing g_instance
    // context) and append our mock extension to that list.
    if (!pLayerName) {
        PFN_vkGetInstanceProcAddr nextGipa = nullptr;
        VkInstance instance = VK_NULL_HANDLE;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            auto physIt = g_physDevToInstance.find(physicalDevice);
            if (physIt != g_physDevToInstance.end()) {
                instance = physIt->second;
            } else if (!g_instanceContexts.empty()) {
                if (g_instanceContexts.size() > 1) {
                    LOGI("vk_layer_swappy_mock: SwappyMock_vkEnumerateDeviceExtensionProperties "
                         "called with unknown physical device and multiple instances active. Using "
                         "arbitrary instance.");
                }
                instance = g_instanceContexts.begin()->first;
            }
            auto instIt = g_instanceContexts.find(instance);
            if (instIt != g_instanceContexts.end()) {
                nextGipa = instIt->second.nextGipa;
            }
        }
        if (!nextGipa || instance == VK_NULL_HANDLE) {
            LOGE("vk_layer_swappy_mock: SwappyMock_vkEnumerateDeviceExtensionProperties called but "
                 "nextGipa or instance is null!");
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        auto nextEnumerate = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
                nextGipa(instance, "vkEnumerateDeviceExtensionProperties"));
        if (!nextEnumerate) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        // Query driver extensions
        uint32_t driverCount = 0;
        VkResult res = nextEnumerate(physicalDevice, nullptr, &driverCount, nullptr);
        if (res != VK_SUCCESS) return res;

        std::vector<VkExtensionProperties> driverExtensions(driverCount);
        res = nextEnumerate(physicalDevice, nullptr, &driverCount, driverExtensions.data());
        if (res != VK_SUCCESS) return res;

        // Add our mock extension if not already present
        bool hasTiming = false;
        for (const auto& ext : driverExtensions) {
            if (std::strcmp(ext.extensionName, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME) == 0) {
                hasTiming = true;
                break;
            }
        }

        std::vector<VkExtensionProperties> allExtensions = driverExtensions;
        if (!hasTiming && g_mockTimingEnabled.load(std::memory_order_relaxed)) {
            allExtensions.push_back(
                    {VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME, VK_MAKE_VERSION(1, 0, 0)});
        }

        if (!pProperties) {
            *pPropertyCount = allExtensions.size();
            LOGI("vk_layer_swappy_mock: "
                 "SwappyMock_vkEnumerateDeviceExtensionProperties count query, "
                 "returning %u",
                 *pPropertyCount);
            return VK_SUCCESS;
        }

        uint32_t toCopy = std::min(*pPropertyCount, (uint32_t)allExtensions.size());
        for (uint32_t i = 0; i < toCopy; ++i) {
            pProperties[i] = allExtensions[i];
        }
        LOGI("vk_layer_swappy_mock: SwappyMock_vkEnumerateDeviceExtensionProperties "
             "properties query, returned %u extensions, timing_supported=%d",
             toCopy, (int)(hasTiming || g_mockTimingEnabled.load(std::memory_order_relaxed)));
        *pPropertyCount = toCopy;
        return (toCopy < allExtensions.size()) ? VK_INCOMPLETE : VK_SUCCESS;
    }

    return VK_ERROR_LAYER_NOT_PRESENT;
}

extern "C" VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName,
                                     uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
    return SwappyMock_vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName,
                                                           pPropertyCount, pProperties);
}

static const VkLayerInstanceCreateInfo* GetLayerInstanceCreateInfo(
        const VkInstanceCreateInfo* pCreateInfo) {
    const VkBaseOutStructure* current =
            reinterpret_cast<const VkBaseOutStructure*>(pCreateInfo->pNext);
    while (current) {
        if (current->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO) {
            auto layerCreateInfo = reinterpret_cast<const VkLayerInstanceCreateInfo*>(current);
            if (layerCreateInfo->function == VK_LAYER_LINK_INFO) {
                return layerCreateInfo;
            }
        }
        current = current->pNext;
    }
    return nullptr;
}

static const VkLayerDeviceCreateInfo* GetLayerDeviceCreateInfo(
        const VkDeviceCreateInfo* pCreateInfo) {
    const VkBaseOutStructure* current =
            reinterpret_cast<const VkBaseOutStructure*>(pCreateInfo->pNext);
    while (current) {
        if (current->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO) {
            auto layerCreateInfo = reinterpret_cast<const VkLayerDeviceCreateInfo*>(current);
            if (layerCreateInfo->function == VK_LAYER_LINK_INFO) {
                return layerCreateInfo;
            }
        }
        current = current->pNext;
    }
    return nullptr;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
SwappyMock_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                            const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
    static thread_local bool in_create = false;
    if (in_create) {
        LOGE("vk_layer_swappy_mock: vkCreateInstance RECURSION DETECTED!");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    in_create = true;

    LOGI("vk_layer_swappy_mock: vkCreateInstance ENTERED!");
    const char* mockTimingEnv = getenv("MOCK_VK_GOOGLE_DISPLAY_TIMING");
    g_mockTimingEnabled.store(mockTimingEnv && std::strcmp(mockTimingEnv, "1") == 0,
                              std::memory_order_relaxed);
    const VkLayerInstanceCreateInfo* chainInfo = GetLayerInstanceCreateInfo(pCreateInfo);
    if (!chainInfo || !chainInfo->u.pLayerInfo ||
        !chainInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr) {
        LOGI("vk_layer_swappy_mock: vkCreateInstance FAILED (bad chain)");
        in_create = false;
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr nextGipa = chainInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;

    LOGI("vk_layer_swappy_mock: vkCreateInstance chainInfo=%p, pLayerInfo=%p, "
         "nextGipa=%p",
         chainInfo, chainInfo->u.pLayerInfo, nextGipa);

    auto* layerInfo = const_cast<VkLayerInstanceLink*>(chainInfo->u.pLayerInfo);
    layerInfo = layerInfo->pNext;
    const_cast<VkLayerInstanceCreateInfo*>(chainInfo)->u.pLayerInfo = layerInfo;

    PFN_vkCreateInstance createFn =
            reinterpret_cast<PFN_vkCreateInstance>(nextGipa(nullptr, "vkCreateInstance"));
    LOGI("vk_layer_swappy_mock: vkCreateInstance resolved createFn=%p", createFn);

    if (!createFn) {
        LOGI("vk_layer_swappy_mock: vkCreateInstance FAILED (no createFn)");
        in_create = false;
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    LOGI("vk_layer_swappy_mock: vkCreateInstance calling realCreateInstance");
    VkResult result = createFn(pCreateInfo, pAllocator, pInstance);
    LOGI("vk_layer_swappy_mock: vkCreateInstance result = %d", result);
    in_create = false;

    if (result != VK_SUCCESS) return result;
    PFN_vkGetInstanceProcAddr gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            nextGipa(*pInstance, "vkGetInstanceProcAddr"));
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_instanceContexts[*pInstance] = {nextGipa};
        g_instanceDispatch[*pInstance] = gipa;
    }
    LOGI("vk_layer_swappy_mock: vkCreateInstance EXITED SUCCESS");
    return VK_SUCCESS;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL SwappyMock_vkEnumeratePhysicalDevices(
        VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) {
    PFN_vkEnumeratePhysicalDevices realEnumerate = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        auto it = g_instanceDispatch.find(instance);
        if (it != g_instanceDispatch.end()) {
            realEnumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
                    it->second(instance, "vkEnumeratePhysicalDevices"));
        }
    }
    if (!realEnumerate) return VK_ERROR_INITIALIZATION_FAILED;
    VkResult res = realEnumerate(instance, pPhysicalDeviceCount, pPhysicalDevices);
    if (res == VK_SUCCESS && pPhysicalDevices && pPhysicalDeviceCount) {
        std::lock_guard<std::mutex> lock(g_lock);
        for (uint32_t i = 0; i < *pPhysicalDeviceCount; ++i) {
            g_physDevToInstance[pPhysicalDevices[i]] = instance;
        }
    }
    return res;
}

extern "C" VKAPI_ATTR void VKAPI_CALL
SwappyMock_vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroyInstance realDestroy = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        auto it = g_instanceDispatch.find(instance);
        if (it != g_instanceDispatch.end()) {
            realDestroy = reinterpret_cast<PFN_vkDestroyInstance>(
                    it->second(instance, "vkDestroyInstance"));
        }
        g_instanceContexts.erase(instance);
        g_instanceDispatch.erase(instance);
        for (auto it = g_physDevToInstance.begin(); it != g_physDevToInstance.end();) {
            if (it->second == instance) {
                it = g_physDevToInstance.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (realDestroy) {
        realDestroy(instance, pAllocator);
    }
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
SwappyMock_vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                          const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    // ARCHITECTURE NOTE ON EXTENSION FILTERING:
    // If a layer implements an extension (such as VK_GOOGLE_display_timing) that the underlying
    // driver does not support, it MUST filter out that extension name from
    // `pCreateInfo->ppEnabledExtensionNames` before calling `realCreateDevice` down the chain.
    // Otherwise, the underlying driver will reject vkCreateDevice with
    // VK_ERROR_EXTENSION_NOT_PRESENT.
    VkDeviceCreateInfo createInfoCopy;
    std::vector<const char*> filteredExtensions;
    const VkDeviceCreateInfo* pRealCreateInfo = pCreateInfo;

    if (pCreateInfo) {
        createInfoCopy = *pCreateInfo;
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
            LOGI("vk_layer_swappy_mock: vkCreateDevice enabled extension %u: %s", i,
                 pCreateInfo->ppEnabledExtensionNames[i]);
            if (std::strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                            VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME) != 0) {
                filteredExtensions.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
            }
        }
        createInfoCopy.enabledExtensionCount = filteredExtensions.size();
        createInfoCopy.ppEnabledExtensionNames = filteredExtensions.data();
        pRealCreateInfo = &createInfoCopy;
    }
    const VkLayerDeviceCreateInfo* chainInfo = GetLayerDeviceCreateInfo(pCreateInfo);
    if (!chainInfo || !chainInfo->u.pLayerInfo ||
        !chainInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    PFN_vkGetDeviceProcAddr nextGdpa = chainInfo->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    PFN_vkGetInstanceProcAddr nextGdpaInstance =
            chainInfo->u.pLayerInfo->pfnNextGetInstanceProcAddr;

    LOGI("vk_layer_swappy_mock: vkCreateDevice chainInfo=%p, pLayerInfo=%p, "
         "nextGdpaInstance=%p",
         chainInfo, chainInfo->u.pLayerInfo, nextGdpaInstance);

    auto* layerInfo = const_cast<VkLayerDeviceLink*>(chainInfo->u.pLayerInfo);
    layerInfo = layerInfo->pNext;
    const_cast<VkLayerDeviceCreateInfo*>(chainInfo)->u.pLayerInfo = layerInfo;

    LOGI("vk_layer_swappy_mock: vkCreateDevice resolved nextGdpaInstance=%p", nextGdpaInstance);
    if (!nextGdpaInstance) {
        LOGE("vk_layer_swappy_mock: vkCreateDevice FAILED because nextGdpaInstance "
             "is NULL!");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkCreateDevice realCreateDevice =
            reinterpret_cast<PFN_vkCreateDevice>(nextGdpaInstance(nullptr, "vkCreateDevice"));
    LOGI("vk_layer_swappy_mock: vkCreateDevice resolved realCreateDevice=%p", realCreateDevice);
    if (!realCreateDevice) return VK_ERROR_INITIALIZATION_FAILED;
    // Store nextGdpa in g_lastNextGdpa BEFORE calling realCreateDevice. During realCreateDevice,
    // libvulkan.so calls InitDispatchTable(dev, get_device_proc_addr_, ...), which queries our
    // layer's vkGetDeviceProcAddr for all core Vulkan commands BEFORE realCreateDevice returns!
    // Storing nextGdpa here allows our fallback path in SwappyMock_vkGetDeviceProcAddr to resolve
    // core Vulkan functions during device initialization.
    g_lastNextGdpa = nextGdpa;
    VkResult result = realCreateDevice(physicalDevice, pRealCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS) return result;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_deviceDispatch[*pDevice] = nextGdpa;
    }
    return VK_SUCCESS;
}

extern "C" VKAPI_ATTR void VKAPI_CALL SwappyMock_vkGetDeviceQueue(VkDevice device,
                                                                  uint32_t queueFamilyIndex,
                                                                  uint32_t queueIndex,
                                                                  VkQueue* pQueue) {
    PFN_vkGetDeviceProcAddr gdpa = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        auto devDispatchIt = g_deviceDispatch.find(device);
        if (devDispatchIt == g_deviceDispatch.end()) return;
        gdpa = devDispatchIt->second;
    }
    PFN_vkGetDeviceQueue realGetDeviceQueue =
            reinterpret_cast<PFN_vkGetDeviceQueue>(gdpa(device, "vkGetDeviceQueue"));
    if (realGetDeviceQueue) {
        realGetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
        if (pQueue && *pQueue != VK_NULL_HANDLE) {
            std::lock_guard<std::mutex> lock(g_lock);
            g_queueToDevice[*pQueue] = device;
        }
    }
}

extern "C" VKAPI_ATTR void VKAPI_CALL
SwappyMock_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroyDevice realDestroy = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        auto it = g_deviceDispatch.find(device);
        if (it != g_deviceDispatch.end()) {
            realDestroy =
                    reinterpret_cast<PFN_vkDestroyDevice>(it->second(device, "vkDestroyDevice"));
        }
        g_deviceDispatch.erase(device);
        for (auto qit = g_queueToDevice.begin(); qit != g_queueToDevice.end();) {
            if (qit->second == device) {
                qit = g_queueToDevice.erase(qit);
            } else {
                ++qit;
            }
        }
    }
    if (realDestroy) {
        realDestroy(device, pAllocator);
    }
}

static std::unordered_map<VkSwapchainKHR, std::vector<VkPastPresentationTimingGOOGLE>>
        g_pastTimings;

VKAPI_ATTR VkResult VKAPI_CALL
Mock_vkGetRefreshCycleDurationGOOGLE(VkDevice device, VkSwapchainKHR swapchain,
                                     VkRefreshCycleDurationGOOGLE* pDisplayTimingProperties) {
    if (pDisplayTimingProperties) {
        pDisplayTimingProperties->refreshDuration = 16666666; // 60Hz
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL Mock_vkGetPastPresentationTimingGOOGLE(
        VkDevice device, VkSwapchainKHR swapchain, uint32_t* pPresentationTimingCount,
        VkPastPresentationTimingGOOGLE* pPresentationTimings) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto& timings = g_pastTimings[swapchain];
    LOGI("Mock_vkGetPastPresentationTimingGOOGLE called. count=%u, pPresentationTimings=%p",
         (unsigned int)timings.size(), pPresentationTimings);
    if (!pPresentationTimings) {
        *pPresentationTimingCount = timings.size();
    } else {
        uint32_t count = std::min(*pPresentationTimingCount, (uint32_t)timings.size());
        for (uint32_t i = 0; i < count; ++i) {
            pPresentationTimings[i] = timings[i];
        }
        timings.erase(timings.begin(), timings.begin() + count);
        *pPresentationTimingCount = count;
        return (count < timings.size()) ? VK_INCOMPLETE : VK_SUCCESS;
    }
    return VK_SUCCESS;
}

extern "C" VKAPI_ATTR void VKAPI_CALL SwappyMock_vkDestroySwapchainKHR(
        VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroySwapchainKHR realDestroy = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_pastTimings.erase(swapchain);
        auto it = g_deviceDispatch.find(device);
        if (it != g_deviceDispatch.end()) {
            realDestroy = reinterpret_cast<PFN_vkDestroySwapchainKHR>(
                    it->second(device, "vkDestroySwapchainKHR"));
        }
    }
    if (realDestroy) {
        realDestroy(device, swapchain, pAllocator);
    }
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
SwappyMock_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    PFN_vkGetDeviceProcAddr gdpa = nullptr;
    VkDevice device = VK_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        auto it = g_queueToDevice.find(queue);
        if (it == g_queueToDevice.end()) return VK_ERROR_INITIALIZATION_FAILED;
        device = it->second;
        auto devDispatchIt = g_deviceDispatch.find(device);
        if (devDispatchIt == g_deviceDispatch.end()) return VK_ERROR_INITIALIZATION_FAILED;
        gdpa = devDispatchIt->second;
    }
    PFN_vkQueuePresentKHR realQueuePresent =
            reinterpret_cast<PFN_vkQueuePresentKHR>(gdpa(device, "vkQueuePresentKHR"));
    if (!realQueuePresent) return VK_ERROR_INITIALIZATION_FAILED;

    bool mockTiming = g_mockTimingEnabled.load(std::memory_order_relaxed);
    LOGI("vkQueuePresentKHR called. mockTiming=%d", mockTiming);
    VkPresentInfoKHR presentInfoCopy;
    const VkPresentInfoKHR* pRealPresentInfo = pPresentInfo;

    if (mockTiming) {
        if (pPresentInfo && pPresentInfo->pNext) {
            const VkBaseInStructure* next =
                    reinterpret_cast<const VkBaseInStructure*>(pPresentInfo->pNext);
            while (next) {
                LOGI("vkQueuePresentKHR: parsing pNext sType=%d", next->sType);
                if (next->sType == VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE) {
                    const VkPresentTimesInfoGOOGLE* timeInfo =
                            reinterpret_cast<const VkPresentTimesInfoGOOGLE*>(next);
                    for (uint32_t i = 0; i < timeInfo->swapchainCount; ++i) {
                        VkPastPresentationTimingGOOGLE timing = {};
                        timing.presentID = timeInfo->pTimes[i].presentID;
                        timing.desiredPresentTime = timeInfo->pTimes[i].desiredPresentTime;
                        timing.actualPresentTime = timing.desiredPresentTime + 16666666;
                        timing.earliestPresentTime = timing.desiredPresentTime;
                        timing.presentMargin = 0;
                        VkSwapchainKHR swapchain =
                                (pPresentInfo->pSwapchains && i < pPresentInfo->swapchainCount)
                                ? pPresentInfo->pSwapchains[i]
                                : VK_NULL_HANDLE;
                        {
                            std::lock_guard<std::mutex> lock(g_lock);
                            g_pastTimings[swapchain].push_back(timing);
                        }
                        LOGI("vkQueuePresentKHR: pushed timing %d", timing.presentID);
                    }
                }
                next = next->pNext;
            }

            // Strip VkPresentTimesInfoGOOGLE from the pNext chain before calling the driver.
            presentInfoCopy = *pPresentInfo;
            const VkBaseInStructure* firstNext =
                    reinterpret_cast<const VkBaseInStructure*>(presentInfoCopy.pNext);
            if (firstNext && firstNext->sType == VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE) {
                presentInfoCopy.pNext = firstNext->pNext;
            } else if (firstNext) {
                // If it's deeper in the chain, log a warning as copying unknown structs is unsafe.
                const VkBaseInStructure* current = firstNext;
                while (current->pNext) {
                    if (current->pNext->sType == VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE) {
                        LOGE("vkQueuePresentKHR: VkPresentTimesInfoGOOGLE is not the first item in "
                             "pNext chain. "
                             "Cannot safely strip it from const chain without knowing struct "
                             "sizes.");
                        break;
                    }
                    current = current->pNext;
                }
            }
            pRealPresentInfo = &presentInfoCopy;
        } else {
            LOGI("vkQueuePresentKHR: pPresentInfo is null or pNext is null");
        }
    }
    return realQueuePresent(queue, pRealPresentInfo);
}

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
SwappyMock_vkGetDeviceProcAddr(VkDevice device, const char* pName);

// Internal implementation of vkGetInstanceProcAddr.
// Note that we check for both "vkGetInstanceProcAddr" and "vkGetDeviceProcAddr".
// Why? The Android Vulkan loader (`libvulkan.so`) queries `vkGetInstanceProcAddr(instance,
// "vkGetDeviceProcAddr")` to obtain the layer's device-level query function. If we don't return our
// own GPA here, the loader falls back to the underlying driver's GPA, bypassing our layer entirely
// for device-level commands!
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
SwappyMock_vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    LOGI("vk_layer_swappy_mock: vkGetInstanceProcAddr (pName=%s)", pName ? pName : "null");
    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkGetInstanceProcAddr);

    // The Vulkan loader calls vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr") to resolve the
    // layer's device-level query function. If we don't return it, the loader will fall back to the
    // driver's GPA, bypassing our layer entirely for device functions!
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkGetDeviceProcAddr);

    if (std::strcmp(pName, "vkCreateInstance") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkCreateInstance);
    if (std::strcmp(pName, "vkDestroyInstance") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkDestroyInstance);
    if (std::strcmp(pName, "vkCreateDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkCreateDevice);
    if (std::strcmp(pName, "vkDestroySwapchainKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkDestroySwapchainKHR);
    if (std::strcmp(pName, "vkEnumeratePhysicalDevices") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkEnumeratePhysicalDevices);
    if (std::strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkEnumerateInstanceLayerProperties);
    if (std::strcmp(pName, "vkEnumerateDeviceLayerProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkEnumerateDeviceLayerProperties);
    if (std::strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(
                SwappyMock_vkEnumerateInstanceExtensionProperties);
    if (std::strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(
                SwappyMock_vkEnumerateDeviceExtensionProperties);
    if (std::strcmp(pName, "vkDestroyDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkDestroyDevice);

    // The loader may resolve device extension functions via vkGetInstanceProcAddr during device
    // creation, so we must expose our mock implementations here as well as in vkGetDeviceProcAddr.
    if (g_mockTimingEnabled.load(std::memory_order_relaxed)) {
        if (std::strcmp(pName, "vkGetRefreshCycleDurationGOOGLE") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(Mock_vkGetRefreshCycleDurationGOOGLE);
        if (std::strcmp(pName, "vkGetPastPresentationTimingGOOGLE") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(Mock_vkGetPastPresentationTimingGOOGLE);
    }
    if (instance != VK_NULL_HANDLE) {
        PFN_vkGetInstanceProcAddr nextGipa = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            auto it = g_instanceDispatch.find(instance);
            if (it != g_instanceDispatch.end()) {
                nextGipa = it->second;
            }
        }
        if (nextGipa) {
            LOGI("vk_layer_swappy_mock: vkGetInstanceProcAddr falling back to next");
            return nextGipa(instance, pName);
        }
    }
    return nullptr;
}

// Internal implementation of vkGetDeviceProcAddr.
// CRITICAL FALLBACK NOTICE:
// During `vkCreateDevice`, immediately before `realCreateDevice(...)` returns to our layer,
// `libvulkan.so` invokes `InitDispatchTable(dev, ...)`, which calls this function for all 40 core
// Vulkan commands. At that moment, `g_deviceDispatch[*pDevice]` is not yet populated! We therefore
// check `g_lastNextGdpa` as a fallback when `g_deviceDispatch.find(device)` fails.
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
SwappyMock_vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    LOGI("vkGetDeviceProcAddr called for %s", pName ? pName : "null");
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkGetDeviceProcAddr);
    if (std::strcmp(pName, "vkQueuePresentKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkQueuePresentKHR);
    if (std::strcmp(pName, "vkGetDeviceQueue") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkGetDeviceQueue);
    if (std::strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkEnumerateInstanceLayerProperties);
    if (std::strcmp(pName, "vkEnumerateDeviceLayerProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkEnumerateDeviceLayerProperties);
    if (std::strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(
                SwappyMock_vkEnumerateDeviceExtensionProperties);
    if (std::strcmp(pName, "vkCreateDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkCreateDevice);
    if (std::strcmp(pName, "vkDestroyDevice") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkDestroyDevice);
    if (std::strcmp(pName, "vkDestroySwapchainKHR") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(SwappyMock_vkDestroySwapchainKHR);

    if (g_mockTimingEnabled.load(std::memory_order_relaxed)) {
        if (std::strcmp(pName, "vkGetRefreshCycleDurationGOOGLE") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(Mock_vkGetRefreshCycleDurationGOOGLE);
        if (std::strcmp(pName, "vkGetPastPresentationTimingGOOGLE") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(Mock_vkGetPastPresentationTimingGOOGLE);
    }

    PFN_vkGetDeviceProcAddr nextGdpa = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        auto it = g_deviceDispatch.find(device);
        if (it != g_deviceDispatch.end()) {
            nextGdpa = it->second;
        }
    }
    if (nextGdpa) return nextGdpa(device, pName);
    PFN_vkGetDeviceProcAddr fallback = g_lastNextGdpa;
    if (fallback) return fallback(device, pName);
    return nullptr;
}

// ==========================================================================================
// EXPORTED ENTRY POINTS (FORWARDING WRAPPERS)
// ==========================================================================================
// Why export thin forwarding wrappers instead of defining `vkGetInstanceProcAddr` directly?
// On Linux/Android, `libvulkan.so` exports `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr`
// globally. If a layer `.so` defines a function literally named `vkGetDeviceProcAddr` and returns
// `reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr)` inside its body, dynamic linking
// rules (ELF symbol interposition) cause `&vkGetDeviceProcAddr` to resolve to `libvulkan.so`'s
// symbol! When the loader stores that pointer in its dispatch table, calling `GetDeviceProcAddr`
// enters an infinite recursion loop inside `libvulkan.so`. By defining `SwappyMock_vk*` internally
// and returning `&SwappyMock_vk*`, we guarantee our layer returns its own internal function
// pointers without symbol preemption.
extern "C" VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,
                                                                               const char* pName) {
    return SwappyMock_vkGetInstanceProcAddr(instance, pName);
}

extern "C" VK_LAYER_EXPORT PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,
                                                                             const char* pName) {
    return SwappyMock_vkGetDeviceProcAddr(device, pName);
}
