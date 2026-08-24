# Swappy Testing Guide

This guide explains how testing works in Swappy and details the various mocking options available. Because Swappy is an Android frame pacing library, it heavily interacts with Android OS components (like the Choreographer, JNI, and hardware-specific Vulkan extensions) that are difficult to test deterministically on emulators. To solve this, Swappy uses several layers of mocking and simulation.

## 1. Testing Core Frame Pacing Logic (`swappycommon_test.cpp`)

To test the core frame pacing and pipelining logic without relying on actual graphics rendering or hardware, Swappy uses simulated workloads.

* **Simulated Threads:** The tests create mock CPU and GPU threads that simulate rendering load. They advance time manually or wait for specified durations.
* **Precise Sleeping:** Emulators are notorious for inaccurate thread sleep times (e.g., `std::this_thread::sleep_for`), which can cause timing-sensitive frame pacing tests to fail. To improve reliability, tests use a custom `preciseSleep` function that sleeps just short of the target time and busy-waits the remaining milliseconds.

## 2. Mocking JNI and the Android Choreographer (`swappy_basic_test.cpp`)

Swappy relies on the Android Choreographer to synchronize frame rendering with display VSYNCs. Testing this on a host or emulator requires intercepting OS calls.

### JNI Mocking
Swappy tests use **Google Mock** to simulate the Android Java runtime (`JNIEnv`).
* A custom `MockJNI` class intercepts calls like `FindClass`, `GetMethodID`, and `CallObjectMethodV`.
* It intercepts the creation of Java objects (like `SwappyDisplayManager`) and fakes responses for SDK version checks and display refresh rates.

### NDK Choreographer Mocking
On newer Android versions, Swappy uses the NDK `AChoreographer` API, which is dynamically loaded from `libandroid.so` at runtime using `dlopen` and `dlsym`.
* **Linker Interception:** The tests use linker flags (`-Wl,--wrap=dlopen` and `-Wl,--wrap=dlsym`) to intercept these dynamic symbol lookups.
* **Simulated Callbacks:** When Swappy looks for `AChoreographer_postFrameCallbackDelayed` or `AChoreographer_postVsyncCallback`, the mock intercepts it and returns a fake function pointer. The mock implementation then spins up a background thread that sleeps for 16ms (simulating 60Hz) before firing the callback back to Swappy.

## 3. Mocking Vulkan Extensions (`vk_layer_swappy_mock.cpp`)

Swappy uses the `VK_GOOGLE_display_timing` extension to get accurate presentation timestamps. Since many emulators and devices don't natively support this extension, Swappy provides a custom Vulkan mock layer.

* **How it works:** The mock layer intercepts `vkQueuePresentKHR`, `vkGetRefreshCycleDurationGOOGLE`, and `vkGetPastPresentationTimingGOOGLE`. When a frame is presented, it records the mock timing in a history buffer, which is later read by Swappy.
* **Enabling the Mock:** The mock timing is toggled by setting the `MOCK_VK_GOOGLE_DISPLAY_TIMING` environment variable to `1`.
* **Vulkan Layer Gotchas:** If you are modifying or writing new mock layers, you must be careful with Android's Vulkan loader (`libvulkan.so`):
  * **Extension Stripping:** The mock layer must strip its own extension name before passing device creation info down the chain to avoid driver conflicts.
  * **Infinite Recursion loops:** Due to ELF symbol preemption, exporting a function literally named `vkGetDeviceProcAddr` can cause infinite recursion loops. Mock layers should use internal names like `SwappyMock_vkGetDeviceProcAddr` and only export thin forwarding wrappers at the bottom of the file.
  * **Dispatch Initialization:** When `libvulkan.so` initializes its dispatch table, it queries the layer immediately before the layer has finished registering it. The mock layer must handle this race condition using a thread-local fallback (`g_lastNextGdpa`).

---

**Summary for Writing New Tests:**
* Use **`SwappyCommonTest`** if you are testing pure timing mathematics.
* Use **`AImageReaderSwapchainTestBase`** if you need to test the lifecycle of Vulkan/OpenGL swapchains with mock JNI/Choreographer responses.
* Use the **`MOCK_VK_GOOGLE_DISPLAY_TIMING`** environment variable if you need to test Vulkan extension fallbacks on an emulator.
