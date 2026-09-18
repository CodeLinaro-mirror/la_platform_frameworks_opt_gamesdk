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

#define LOG_TAG "SwappyTestRenderer"

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <strings.h>
#include <sys/system_properties.h>
#include <vulkan/vulkan.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Circle.h"
#include "DvfsBenchmark.h"
#include "Log.h"
#include "OverlayBatcher.h"
#include "TelemetryRingBuffer.h"
#include "Trace.h"
#include "gles/GlesCircleRenderer.h"
#include "gles/GlesContext.h"
#include "gles/GlesGpuCalibrationTarget.h"
#include "gles/GlesOverlayRenderer.h"
#include "swappy/swappyGL.h"
#include "swappy/swappyGL_extra.h"
#include "swappy/swappyVk.h"
#include "vulkan/VulkanCircleRenderer.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/VulkanFrameManager.h"
#include "vulkan/VulkanGpuCalibrationTarget.h"
#include "vulkan/VulkanOverlayRenderer.h"
#include "vulkan/VulkanSwapchain.h"

namespace {

using namespace std::chrono_literals;

static void swappyPostWaitCallback(void* /* userData */, int64_t cpu_time_ns, int64_t gpu_time_ns) {
    record_swappy_post_wait_sample(cpu_time_ns, gpu_time_ns);
}

static std::string sanitizeTraceField(const char* raw) {
    if (!raw) return "";
    std::string out;
    for (const char* p = raw; *p != '\0'; ++p) {
        char c = *p;
        if (c == ';' || c == '|' || c == '\n' || c == '\r') {
            c = ' ';
        }
        out.push_back(c);
    }
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

static std::string readSysProp(const char* key) {
    char buf[PROP_VALUE_MAX] = {0};
    __system_property_get(key, buf);
    return sanitizeTraceField(buf);
}

static std::string formatVulkanVendor(uint32_t vendorId) {
    char buf[64];
    switch (vendorId) {
        case 0x13B5:
            std::snprintf(buf, sizeof(buf), "ARM (0x%04x)", vendorId);
            break;
        case 0x5143:
            std::snprintf(buf, sizeof(buf), "Qualcomm (0x%04x)", vendorId);
            break;
        case 0x1010:
            std::snprintf(buf, sizeof(buf), "ImgTec (0x%04x)", vendorId);
            break;
        case 0x1002:
            std::snprintf(buf, sizeof(buf), "AMD (0x%04x)", vendorId);
            break;
        case 0x10DE:
            std::snprintf(buf, sizeof(buf), "NVIDIA (0x%04x)", vendorId);
            break;
        case 0x8086:
            std::snprintf(buf, sizeof(buf), "Intel (0x%04x)", vendorId);
            break;
        default:
            std::snprintf(buf, sizeof(buf), "0x%04x", vendorId);
            break;
    }
    return std::string(buf);
}

static std::string buildDeviceInfoTraceSlice(const char* apiName, const char* pacingName,
                                             const char* vendorRaw, const char* rendererRaw,
                                             const char* versionRaw) {
    const std::string api = sanitizeTraceField(apiName);
    const std::string pacing = sanitizeTraceField(pacingName);
    const std::string model = readSysProp("ro.product.model");
    const std::string device = readSysProp("ro.product.device");
    const std::string release = readSysProp("ro.build.version.release");
    const std::string sdk = readSysProp("ro.build.version.sdk");
    const std::string buildId = readSysProp("ro.build.id");
    const std::string soc = readSysProp("ro.soc.model");
    const std::string egl = readSysProp("ro.hardware.egl");
    const std::string glVendor = sanitizeTraceField(vendorRaw);
    const std::string glRenderer = sanitizeTraceField(rendererRaw);
    const std::string glVersion = sanitizeTraceField(versionRaw);

    char slice[1024];
    std::snprintf(slice, sizeof(slice),
                  "Swappy.DeviceInfo|api=%s;pacing=%s;model=%s;device=%s;soc=%s;release=%s;"
                  "sdk=%s;build_id=%s;egl=%s;gl_vendor=%s;gl_renderer=%s;gl_version=%s",
                  api.c_str(), pacing.c_str(), model.c_str(), device.c_str(), soc.c_str(),
                  release.c_str(), sdk.c_str(), buildId.c_str(), egl.c_str(), glVendor.c_str(),
                  glRenderer.c_str(), glVersion.c_str());
    return std::string(slice);
}

// Classic Swappy pacing: measured frame times drive the auto swap interval.
// Must match PACING_CLASSIC in SwappyTestActivity.java.
constexpr int kPacingClassic = 0;

// Sample-only pacing value: Swappy is never initialized and frames go
// straight to eglSwapBuffers / vkQueuePresentKHR, giving an unpaced
// baseline. Must match PACING_NO_SWAPPY in SwappyTestActivity.java.
constexpr int kPacingNoSwappy = 3;

struct TouchDragState {
    std::atomic<bool> isDragging{false};
    std::atomic<float> touchNdcX{0.0f};
    std::atomic<float> touchNdcY{0.0f};
    std::atomic<int64_t> touchEventTimeNs{0};
    std::atomic<bool> manualModeActive{false};
    std::atomic<bool> histogramMode{false};
    std::atomic<int32_t> activeInputCookie{0};

    static void ScreenToNdc(float px, float py, int32_t screenW, int32_t screenH, float& outX,
                            float& outY) noexcept {
        const float w = std::max(1.0f, static_cast<float>(screenW));
        const float h = std::max(1.0f, static_cast<float>(screenH));
        outX = (px / w) * 2.0f - 1.0f;
        outY = 1.0f - (py / h) * 2.0f;
    }

    static bool HitTestCircle(float ndcX, float ndcY, float circleX, float circleY, float radius,
                              float aspect) noexcept {
        const float effectiveAspect = (aspect > 0.0f) ? aspect : 1.0f;
        const float dx = (ndcX - circleX) * effectiveAspect;
        const float dy = (ndcY - circleY);
        const float touchRadius = radius + 0.12f;
        return (dx * dx + dy * dy) <= (touchRadius * touchRadius);
    }

    static bool HitTestGraphPanel(float py, int32_t screenW, int32_t screenH) noexcept {
        const float w = std::max(1.0f, static_cast<float>(screenW));
        const float h = std::max(1.0f, static_cast<float>(screenH));
        const float densityScale = std::clamp(std::min(w, h) / 1080.0f, 0.75f, 2.5f);
        const float margin = 18.0f * densityScale;
        const float freqPanelTop = margin + 40.0f * densityScale;
        const float freqPanelHeight = 310.0f * densityScale;
        const float graphPanelHeight = std::min(420.0f * densityScale, h * 0.34f);
        const float graphPanelTop = h - margin - graphPanelHeight;
        if (graphPanelTop <= freqPanelTop + freqPanelHeight + 12.0f * densityScale) {
            return false;
        }
        return py >= graphPanelTop && py <= (h - margin);
    }
};

class SwappyTestRenderer {
public:
    static SwappyTestRenderer& getInstance() {
        static SwappyTestRenderer instance;
        return instance;
    }

    void init(JNIEnv* env, jobject activity) {
        {
            std::lock_guard<std::mutex> lock(mJniMutex);
            env->GetJavaVM(&mJavaVM);
            if (mActivityGlobalRef != nullptr) {
                env->DeleteGlobalRef(mActivityGlobalRef);
                mActivityGlobalRef = nullptr;
            }
            if (activity != nullptr) {
                mActivityGlobalRef = env->NewGlobalRef(activity);
            }
        }

        if (!mVkTracerInjected.exchange(true)) {
            SwappyTracer tracer = {};
            tracer.postWait = swappyPostWaitCallback;
            SwappyVk_injectTracer(&tracer);
        }

        if (!mRenderApiExplicitlySet.load()) {
            const std::string apiProp = readSysProp("debug.swappytest.api");
            if (!apiProp.empty()) {
                const bool isVk = (strcasecmp(apiProp.c_str(), "vulkan") == 0 ||
                                   strcasecmp(apiProp.c_str(), "vk") == 0 ||
                                   strcmp(apiProp.c_str(), "1") == 0);
                set_active_render_api(isVk ? SWAPPY_RENDER_API_VULKAN : SWAPPY_RENDER_API_GLES);
            }
        }

        const std::string overlayProp = readSysProp("debug.swappytest.overlay");
        if (!overlayProp.empty()) {
            const bool showOverlay = (strcasecmp(overlayProp.c_str(), "true") == 0 ||
                                      strcasecmp(overlayProp.c_str(), "on") == 0 ||
                                      strcmp(overlayProp.c_str(), "1") == 0);
            mOverlayEnabled.store(showOverlay);
        }
    }

    void setOverlayEnabled(bool enabled) {
        mOverlayEnabled.store(enabled);
    }

    void toggleHistogramMode() noexcept {
        mTouchDrag.histogramMode.store(!mTouchDrag.histogramMode.load());
    }

    void onPointerDown(float px, float py, int64_t eventTimeNs, bool isManual) {
        mTouchDrag.manualModeActive.store(isManual);
        mTouchDrag.touchEventTimeNs.store(eventTimeNs);
        const SurfaceSize sz = mSurfaceSize.load();
        if (mOverlayEnabled.load() && TouchDragState::HitTestGraphPanel(py, sz.width, sz.height)) {
            toggleHistogramMode();
            return;
        }

        if (!isManual) return;

        float ndcX = 0.0f;
        float ndcY = 0.0f;
        TouchDragState::ScreenToNdc(px, py, sz.width, sz.height, ndcX, ndcY);

        const float aspect =
                sz.height > 0 ? static_cast<float>(sz.width) / static_cast<float>(sz.height) : 1.0f;
        const float curCircleX = mCurrentCircleX.load();
        const float curCircleY = mCurrentCircleY.load();
        const float curRadius = mCurrentCircleRadius.load();

        if (TouchDragState::HitTestCircle(ndcX, ndcY, curCircleX, curCircleY, curRadius, aspect)) {
            mTouchDrag.touchNdcX.store(ndcX);
            mTouchDrag.touchNdcY.store(ndcY);
            mTouchDrag.isDragging.store(true);
        }

        emitInputTrace(0, px, py);
    }

    void onPointerMove(float px, float py, int64_t eventTimeNs) {
        if (!mTouchDrag.isDragging.load()) return;
        mTouchDrag.touchEventTimeNs.store(eventTimeNs);
        const SurfaceSize sz = mSurfaceSize.load();
        float ndcX = 0.0f;
        float ndcY = 0.0f;
        TouchDragState::ScreenToNdc(px, py, sz.width, sz.height, ndcX, ndcY);
        mTouchDrag.touchNdcX.store(std::clamp(ndcX, -0.85f, 0.85f));
        mTouchDrag.touchNdcY.store(std::clamp(ndcY, -0.85f, 0.85f));

        emitInputTrace(2, px, py);
    }

    void onPointerUp(float px, float py, int64_t eventTimeNs) {
        mTouchDrag.touchEventTimeNs.store(eventTimeNs);
        mTouchDrag.isDragging.store(false);

        emitInputTrace(1, px, py);
    }

    void emitInputTrace(int action, float px, float py) {
        if (auto* trace = samples::Trace::getInstance()) {
            if (trace->isEnabled()) {
                char buf[128];
                std::snprintf(buf, sizeof(buf),
                              "SwappyTest::OnTouchEvent(action=%d, x=%.1f, y=%.1f)", action, px,
                              py);
                trace->beginSection(buf);
                trace->endSection();

                const int32_t cookie = ++mInputCookieCounter;
                mTouchDrag.activeInputCookie.store(cookie);
                trace->beginAsyncSection("SwappyTest::InputLatency", cookie);

                trace->setCounter("SwappyTest::TouchDragActive",
                                  mTouchDrag.isDragging.load() ? 1 : 0);
                trace->setCounter("SwappyTest::TouchNdcX_x1000",
                                  static_cast<int64_t>(mTouchDrag.touchNdcX.load() * 1000.0f));
                trace->setCounter("SwappyTest::TouchNdcY_x1000",
                                  static_cast<int64_t>(mTouchDrag.touchNdcY.load() * 1000.0f));
            }
        }
    }

    void setRenderApi(SwappyRenderApi api) {
        mRenderApiExplicitlySet.store(true);
        set_active_render_api(api);
    }

    SwappyRenderApi getRenderApi() const {
        return get_active_render_api();
    }

    void setOptions(bool swappyEnabled, bool set30FpsLimit) {
        mSwappyEnabled.store(swappyEnabled);
        mSet30FpsLimit.store(set30FpsLimit);
        set_user_30fps_limit(set30FpsLimit);
        if (swappyEnabled) {
            // The 30 FPS limit only sets the shortest swap interval Swappy may
            // use. Auto swap interval stays on so its ladder is still exercised.
            setSwapIntervalNs(set30FpsLimit ? SWAPPY_SWAP_30FPS : SWAPPY_SWAP_60FPS);
            setAutoSwapInterval(mAutoSwapInterval.load());
            setAutoPipelineFlagOnly(mAutoPipelineMode.load());
            if (mBufferStuffingFixWait.load() >= 0) {
                setBufferStuffingFixWait(mBufferStuffingFixWait.load());
            }
            if (mFenceTimeoutMs.load() >= 0) {
                setFenceTimeoutMs(mFenceTimeoutMs.load());
            }
        }
    }

    void setSwapIntervalNs(uint64_t swapNs) {
        mSwapIntervalNs.store(swapNs);
        mVkCurrentSwapIntervalNs.store(swapNs);
        mVkConfigDirty.store(true);
        if (!isNoSwappy() && getRenderApi() == SWAPPY_RENDER_API_GLES) {
            SwappyGL_setSwapIntervalNS(swapNs);
        }
    }

    void setPipelineMode(const char* mode) {
        const char* norm = "auto";
        if (mode != nullptr) {
            if (strcasecmp(mode, "off") == 0 || strcasecmp(mode, "false") == 0 ||
                strcmp(mode, "0") == 0) {
                norm = "off";
            } else if (strcasecmp(mode, "on") == 0) {
                norm = "on";
            }
        }
        setenv("SWAPPY_PIPELINE_MODE", norm, 1);
        const bool isAuto = (strcmp(norm, "auto") == 0);
        setAutoPipelineFlagOnly(isAuto);
    }

    void setAutoPipelineFlagOnly(bool isAuto) {
        mAutoPipelineMode.store(isAuto);
        mVkConfigDirty.store(true);
        if (isNoSwappy()) return;
        if (getRenderApi() == SWAPPY_RENDER_API_GLES) {
            SwappyGL_setAutoPipelineMode(isAuto);
        } else {
            SwappyVk_setAutoPipelineMode(isAuto);
        }
    }

    void setAutoSwapInterval(bool enabled) {
        mAutoSwapInterval.store(enabled);
        mVkConfigDirty.store(true);
        if (isNoSwappy()) return;
        if (getRenderApi() == SWAPPY_RENDER_API_GLES) {
            SwappyGL_setAutoSwapInterval(enabled);
        } else {
            SwappyVk_setAutoSwapInterval(enabled);
        }
    }

    void setBufferStuffingFixWait(int32_t nFrames) {
        mBufferStuffingFixWait.store(nFrames);
        if (!isNoSwappy() && nFrames >= 0 && getRenderApi() == SWAPPY_RENDER_API_GLES) {
            SwappyGL_setBufferStuffingFixWait(nFrames);
        }
    }

    void setFenceTimeoutMs(int32_t timeoutMs) {
        mFenceTimeoutMs.store(timeoutMs);
        mVkConfigDirty.store(true);
        if (!isNoSwappy() && timeoutMs >= 0) {
            const uint64_t timeoutNs = static_cast<uint64_t>(timeoutMs) * 1000000ULL;
            if (getRenderApi() == SWAPPY_RENDER_API_GLES) {
                SwappyGL_setFenceTimeoutNS(timeoutNs);
            } else {
                SwappyVk_setFenceTimeoutNS(timeoutNs);
            }
        }
    }

    void resetFramePacing() {
        if (isNoSwappy()) return;
        if (getRenderApi() == SWAPPY_RENDER_API_VULKAN) {
            mPendingVkResetPacing.store(true);
        } else {
            SwappyGL_resetFramePacing();
        }
    }

    // Takes kPacingClassic or kPacingNoSwappy. The render loop reads the
    // choice when it starts, so a switch applies from the next run; leaving
    // No Swappy re-initializes Swappy with the stored settings.
    void setPacingStrategy(int strategy) {
        mPacingStrategy.store(strategy == kPacingNoSwappy ? kPacingNoSwappy : kPacingClassic);
    }

    bool isNoSwappy() const {
        return mPacingStrategy.load() == kPacingNoSwappy;
    }

    const char* getPacingStrategyName() const {
        return isNoSwappy() ? "no_swappy" : "classic";
    }

    void start(ANativeWindow* window, int32_t width, int32_t height, bool autoStart = false) {
        stop();
        if (!window) return;

        mWindow = window;
        setSurfaceSize(width > 0 ? width : ANativeWindow_getWidth(window),
                       height > 0 ? height : ANativeWindow_getHeight(window));
        mRunning.store(true);

        if (autoStart) {
            start_calibration_thread();
        }
        mRenderThread = std::thread(&SwappyTestRenderer::renderLoop, this);
    }

    void stop() {
        mRunning.store(false);
        if (mRenderThread.joinable()) {
            mRenderThread.join();
        }
        stop_calibration_thread();
        if (mWindow) {
            SwappyGL_setWindow(nullptr);
            ANativeWindow_release(mWindow);
            mWindow = nullptr;
        }
    }

    // The UI thread calls this while the render loop draws; the loop uses the
    // new size from its next frame.
    void setSurfaceSize(int32_t width, int32_t height) {
        mSurfaceSize.store({width, height});
        set_render_surface_size(width, height);
    }

    // The swap interval Swappy currently paces at, or 0 when unknown. Only
    // the HUD and isFpsSupported read it; runs are scored on the host from
    // the Swappy.TargetPeriodNs trace counter. SwappyVk needs the swapchain,
    // which the render thread owns, so Vulkan reads the value the render
    // loop caches after each present.
    uint64_t currentSwapIntervalNs() const {
        const uint64_t ns = (getRenderApi() == SWAPPY_RENDER_API_VULKAN || isNoSwappy())
                ? mVkCurrentSwapIntervalNs.load()
                : SwappyGL_getSwapIntervalNS();
        return ns == static_cast<uint64_t>(-1) ? 0 : ns;
    }

    uint64_t getRefreshDurationNs() const {
        // Without Swappy there is no measured refresh period; fall back to the
        // last one Swappy reported in this process, else 60 Hz.
        const bool querySwappyGl = !isNoSwappy();
        uint64_t refresh = (getRenderApi() == SWAPPY_RENDER_API_VULKAN || !querySwappyGl)
                ? mVkRefreshDurationNs.load()
                : SwappyGL_getRefreshPeriodNanos();
        if (refresh == 0) {
            refresh = mVkRefreshDurationNs.load();
        }
        if (refresh == 0 && querySwappyGl) {
            refresh = SwappyGL_getRefreshPeriodNanos();
        }
        return refresh > 0 ? refresh : 16666666ULL;
    }

    bool isFpsSupported(int fps) const {
        if (fps <= 0) return false;
        const double targetPeriodNs = 1e9 / static_cast<double>(fps);
        const uint64_t baseMinSwapNs = (mSet30FpsLimit.load() || is_scenario_explicit_30fps())
                ? SWAPPY_SWAP_30FPS
                : SWAPPY_SWAP_60FPS;
        const bool querySwappyGl = getRenderApi() != SWAPPY_RENDER_API_VULKAN && !isNoSwappy();
        const uint64_t currentSwapNs = currentSwapIntervalNs();
        const uint64_t minAllowedNs = (currentSwapNs > 0 && currentSwapNs < baseMinSwapNs)
                ? currentSwapNs
                : baseMinSwapNs;

        // Cannot swap faster than configured minimum swap interval (with 0.5ms margin)
        if (targetPeriodNs + 500000.0 < static_cast<double>(minAllowedNs)) {
            return false;
        }

        uint64_t periods[16] = {0};
        int count = 0;
        if (querySwappyGl) {
            count = SwappyGL_getSupportedRefreshPeriodsNS(periods, 16);
        }
        if (count <= 0) {
            count = mVkSupportedRefreshPeriodCount.load();
            for (int i = 0; i < count && i < 16; ++i) {
                periods[i] = mVkSupportedRefreshPeriodsNs[i].load();
            }
        }
        if (count <= 0) {
            periods[0] = getRefreshDurationNs();
            count = 1;
        }
        for (int i = 0; i < count && i < 16; ++i) {
            if (periods[i] == 0) continue;
            double ratio = targetPeriodNs / static_cast<double>(periods[i]);
            double nearestInt = std::round(ratio);
            if (nearestInt >= 1.0 && std::abs(ratio - nearestInt) < 0.06) {
                return true;
            }
        }
        return false;
    }

    std::array<bool, samples::TelemetrySnapshot::kNumFpsGuidelines> getSupportedFpsFlags() const {
        std::array<bool, samples::TelemetrySnapshot::kNumFpsGuidelines> flags{};
        for (size_t i = 0; i < samples::TelemetrySnapshot::kNumFpsGuidelines; ++i) {
            flags[i] = isFpsSupported(samples::TelemetrySnapshot::kFpsGuidelines[i]);
        }
        return flags;
    }

private:
    SwappyTestRenderer() = default;

    void renderLoop() {
        if (getRenderApi() == SWAPPY_RENDER_API_VULKAN) {
            renderLoopVulkan();
        } else {
            renderLoopGles();
        }
    }

    bool initSwappyGl(JNIEnv* env) {
        if (env == nullptr) {
            ALOGE("Cannot initialize SwappyGL without a valid JNIEnv");
            return false;
        }
        jobject activityRef = nullptr;
        {
            std::lock_guard<std::mutex> lock(mJniMutex);
            activityRef = mActivityGlobalRef;
        }
        if (activityRef == nullptr) {
            ALOGE("Cannot initialize SwappyGL without an Activity reference");
            return false;
        }

        SwappyGL_destroy();
        if (!SwappyGL_init(env, activityRef)) {
            ALOGE("SwappyGL_init failed");
            return false;
        }

        SwappyGL_setSwapIntervalNS(mSwapIntervalNs.load());
        SwappyGL_setAutoSwapInterval(mAutoSwapInterval.load());
        SwappyGL_setAutoPipelineMode(mAutoPipelineMode.load());
        if (mBufferStuffingFixWait.load() >= 0) {
            SwappyGL_setBufferStuffingFixWait(mBufferStuffingFixWait.load());
        }
        if (mFenceTimeoutMs.load() >= 0) {
            SwappyGL_setFenceTimeoutNS(static_cast<uint64_t>(mFenceTimeoutMs.load()) * 1000000ULL);
        }
        SwappyGL_enableStats(true);

        SwappyTracer tracer = {};
        tracer.postWait = swappyPostWaitCallback;
        SwappyGL_injectTracer(&tracer);

        const uint64_t refreshNs = SwappyGL_getRefreshPeriodNanos();
        if (refreshNs > 0) {
            mVkRefreshDurationNs.store(refreshNs);
        }
        uint64_t periods[16] = {0};
        const int numPeriods = SwappyGL_getSupportedRefreshPeriodsNS(periods, 16);
        if (numPeriods > 0) {
            const int capped = (numPeriods > 16) ? 16 : numPeriods;
            for (int i = 0; i < capped; ++i) {
                mVkSupportedRefreshPeriodsNs[i].store(periods[i]);
            }
            mVkSupportedRefreshPeriodCount.store(capped);
        } else if (refreshNs > 0) {
            mVkSupportedRefreshPeriodsNs[0].store(refreshNs);
            mVkSupportedRefreshPeriodCount.store(1);
        }
        return true;
    }

    bool initSwappyVkSwapchain(JNIEnv* env, samples::vulkan::VulkanContext& context,
                               samples::vulkan::VulkanSwapchain& swapchain) {
        if (env == nullptr) {
            ALOGE("Cannot initialize SwappyVk without a valid JNIEnv");
            return false;
        }
        jobject activityRef = nullptr;
        {
            std::lock_guard<std::mutex> lock(mJniMutex);
            activityRef = mActivityGlobalRef;
        }
        if (activityRef == nullptr) {
            ALOGE("Cannot initialize SwappyVk without an Activity reference");
            return false;
        }

        VkDevice device = context.GetDevice();
        VkSwapchainKHR vkSwapchain = swapchain.GetSwapchain();
        uint64_t refreshDurationNs = 0;
        if (!SwappyVk_initAndGetRefreshCycleDuration(env, activityRef, context.GetPhysicalDevice(),
                                                     device, vkSwapchain, &refreshDurationNs)) {
            ALOGE("SwappyVk_initAndGetRefreshCycleDuration failed");
            return false;
        }

        if (refreshDurationNs > 0) {
            mVkRefreshDurationNs.store(refreshDurationNs);
        }
        SwappyVk_setWindow(device, vkSwapchain, mWindow);

        SwappyVk_setSwapIntervalNS(device, vkSwapchain, mSwapIntervalNs.load());
        SwappyVk_setAutoSwapInterval(mAutoSwapInterval.load());
        SwappyVk_setAutoPipelineMode(mAutoPipelineMode.load());
        if (mFenceTimeoutMs.load() >= 0) {
            SwappyVk_setFenceTimeoutNS(static_cast<uint64_t>(mFenceTimeoutMs.load()) * 1000000ULL);
        }
        SwappyVk_enableStats(vkSwapchain, true);

        uint64_t periods[16] = {0};
        const int numPeriods = SwappyVk_getSupportedRefreshPeriodsNS(periods, 16, vkSwapchain);
        if (numPeriods > 0) {
            const int capped = (numPeriods > 16) ? 16 : numPeriods;
            for (int i = 0; i < capped; ++i) {
                mVkSupportedRefreshPeriodsNs[i].store(periods[i]);
            }
            mVkSupportedRefreshPeriodCount.store(capped);
        } else if (refreshDurationNs > 0) {
            mVkSupportedRefreshPeriodsNs[0].store(refreshDurationNs);
            mVkSupportedRefreshPeriodCount.store(1);
        }

        mVkCurrentSwapIntervalNs.store(SwappyVk_getSwapIntervalNS(vkSwapchain));
        mVkConfigDirty.store(false);
        return true;
    }

    void applyPendingSwappyVkUpdates(VkDevice device, VkSwapchainKHR vkSwapchain) {
        if (mVkConfigDirty.exchange(false)) {
            SwappyVk_setSwapIntervalNS(device, vkSwapchain, mSwapIntervalNs.load());
            SwappyVk_setAutoSwapInterval(mAutoSwapInterval.load());
            SwappyVk_setAutoPipelineMode(mAutoPipelineMode.load());
            if (mFenceTimeoutMs.load() >= 0) {
                SwappyVk_setFenceTimeoutNS(static_cast<uint64_t>(mFenceTimeoutMs.load()) *
                                           1000000ULL);
            }
        }
        if (mPendingVkResetPacing.exchange(false)) {
            SwappyVk_resetFramePacing(vkSwapchain);
        }
    }

    // Vulkan objects owned by the render thread for the duration of renderLoopVulkan().
    // Members are declared in the original construction order so that destructors run in
    // the same (reverse) order.
    struct VulkanRenderState {
        samples::vulkan::VulkanContext context;
        samples::vulkan::VulkanSwapchain swapchain;
        samples::vulkan::VulkanFrameManager frameManager;
        samples::vulkan::VulkanCircleRenderer circleRenderer;
        samples::vulkan::VulkanOverlayRenderer overlayRenderer;
        JNIEnv* env = nullptr;
        bool swappyVkReady = false;
    };

    // Ball position and velocity, advanced each frame by touch drag or bouncing.
    struct BallMotionState {
        float x = 0.0f;
        float y = 0.0f;
        float velocity = 1.2f;
        float prevTouchX = 0.0f;
        bool wasDragging = false;
    };

    struct CirclePosition {
        float x;
        float y;
    };

    JNIEnv* attachRenderThreadToJvm(bool& attachedJvm) {
        JNIEnv* env = nullptr;
        std::lock_guard<std::mutex> lock(mJniMutex);
        if (mJavaVM != nullptr) {
            const jint getEnvRes = mJavaVM->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
            if (getEnvRes == JNI_EDETACHED) {
                if (mJavaVM->AttachCurrentThread(&env, nullptr) == JNI_OK) {
                    attachedJvm = true;
                }
            }
        }
        return env;
    }

    void detachRenderThreadFromJvm(bool& attachedJvm) {
        if (attachedJvm && mJavaVM != nullptr) {
            mJavaVM->DetachCurrentThread();
            attachedJvm = false;
        }
    }

    // Creates the Vulkan instance, surface, device, swapchain, SwappyVk binding and renderers.
    // On failure, destroys whatever was created, stops the render loop and returns false.
    bool initVulkanRenderState(VulkanRenderState& vk) {
        if (!vk.context.InitializeInstance()) {
            ALOGE("VulkanContext::InitializeInstance failed");
            mRunning.store(false);
            return false;
        }

        if (!vk.swapchain.InitializeSurface(&vk.context, mWindow)) {
            ALOGE("VulkanSwapchain::InitializeSurface failed");
            mRunning.store(false);
            return false;
        }

        if (!vk.context.InitializeDevice(vk.swapchain.GetSurface())) {
            ALOGE("VulkanContext::InitializeDevice failed");
            vk.swapchain.Destroy();
            vk.context.Destroy();
            mRunning.store(false);
            return false;
        }

        const SurfaceSize initialSize = mSurfaceSize.load();
        const uint32_t initW = initialSize.width > 0
                ? static_cast<uint32_t>(initialSize.width)
                : static_cast<uint32_t>(ANativeWindow_getWidth(mWindow));
        const uint32_t initH = initialSize.height > 0
                ? static_cast<uint32_t>(initialSize.height)
                : static_cast<uint32_t>(ANativeWindow_getHeight(mWindow));
        if (!vk.swapchain.CreateSwapchain(initW, initH)) {
            ALOGE("VulkanSwapchain::CreateSwapchain failed");
            vk.swapchain.Destroy();
            vk.context.Destroy();
            mRunning.store(false);
            return false;
        }

        // No Swappy runs never register the swapchain with SwappyVk, so every
        // frame below takes the raw vkQueuePresentKHR path.
        vk.swappyVkReady = !isNoSwappy() && initSwappyVkSwapchain(vk.env, vk.context, vk.swapchain);

        vk.frameManager.Initialize(&vk.context, vk.swapchain.GetImageCount(),
                                   vk.swapchain.GetImageCount());

        if (!vk.circleRenderer.Initialize(vk.context, vk.swapchain)) {
            ALOGE("VulkanCircleRenderer::Initialize failed");
            vk.frameManager.Destroy();
            vk.swapchain.Destroy();
            vk.context.Destroy();
            mRunning.store(false);
            return false;
        }

        if (!vk.overlayRenderer.Initialize(&vk.context, vk.circleRenderer.GetRenderPass())) {
            ALOGE("VulkanOverlayRenderer::Initialize failed");
        }
        return true;
    }

    std::string buildVulkanDeviceInfoSlice(const samples::vulkan::VulkanContext& context) {
        const VkPhysicalDeviceProperties& props = context.GetPhysicalDeviceProperties();
        const std::string vkVendorStr = formatVulkanVendor(props.vendorID);
        char vkVersionBuf[128];
        std::snprintf(vkVersionBuf, sizeof(vkVersionBuf), "Vulkan %u.%u.%u (driver 0x%x)",
                      VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
                      VK_VERSION_PATCH(props.apiVersion), props.driverVersion);
        return buildDeviceInfoTraceSlice("vulkan", getPacingStrategyName(), vkVendorStr.c_str(),
                                         props.deviceName, vkVersionBuf);
    }

    // Re-emits the device info trace slice when tracing starts, the scenario step changes,
    // or every 60 frames.
    void traceDeviceInfoIfNeeded(const std::string& deviceInfoSlice, uint64_t frameCounter,
                                 bool& wasTracing, int& prevStepIndex) {
        bool isTracing = false;
        if (auto* trace = samples::Trace::getInstance()) {
            isTracing = trace->isAvailable() && trace->isEnabled();
        }
        const int curStepIndex = get_current_step_index();
        if (isTracing &&
            (!wasTracing || curStepIndex != prevStepIndex || (frameCounter % 60 == 0))) {
            samples::ScopedTrace deviceInfoTrace(deviceInfoSlice.c_str());
        }
        wasTracing = isTracing;
        prevStepIndex = curStepIndex;
    }

    // Recreates the swapchain at the given size and rebinds SwappyVk, semaphores and
    // framebuffers to it.
    void recreateVulkanSwapchain(VulkanRenderState& vk, uint32_t width, uint32_t height) {
        vk.context.WaitIdle();
        if (vk.swapchain.CreateSwapchain(width, height)) {
            if (vk.swappyVkReady) {
                initSwappyVkSwapchain(vk.env, vk.context, vk.swapchain);
            }
            vk.frameManager.EnsureRenderFinishedSemaphores(vk.swapchain.GetImageCount());
            vk.circleRenderer.RecreateFramebuffers(vk.context.GetDevice(), vk.swapchain);
        }
    }

    // Follows the touch drag when active; otherwise bounces horizontally and eases y to 0.
    void updateBallMotion(BallMotionState& ball, float dt) {
        const bool isDragging = mTouchDrag.isDragging.load();
        if (isDragging) {
            const float touchX = mTouchDrag.touchNdcX.load();
            const float touchY = mTouchDrag.touchNdcY.load();
            if (ball.wasDragging && dt > 0.001f) {
                const float instantV = (touchX - ball.prevTouchX) / dt;
                ball.velocity = 0.7f * ball.velocity + 0.3f * std::clamp(instantV, -3.0f, 3.0f);
            }
            ball.prevTouchX = touchX;
            ball.x = std::clamp(touchX, -0.75f, 0.75f);
            ball.y = std::clamp(touchY, -0.75f, 0.75f);
            ball.wasDragging = true;
        } else {
            if (ball.wasDragging) {
                ball.wasDragging = false;
                if (std::abs(ball.velocity) < 0.2f) {
                    ball.velocity = (ball.x >= 0.0f) ? -1.2f : 1.2f;
                }
            }
            ball.x += ball.velocity * dt;
            if (ball.x > 0.75f) {
                ball.velocity = -std::abs(ball.velocity);
                ball.x = 0.75f;
            } else if (ball.x < -0.75f) {
                ball.velocity = std::abs(ball.velocity);
                ball.x = -0.75f;
            }
            ball.y += (0.0f - ball.y) * std::min(1.0f, dt * 5.0f);
        }
    }

    static std::vector<samples::Circle> makeCalibrationCircles() {
        return {{samples::Circle::Color{0.0f, 0.9f, 0.75f}, 0.65f, 0.0f, 0.0f}};
    }

    // Builds the circle and overlay HUD for this frame and records them into `cmd`.
    // Returns the drawn circle center.
    CirclePosition recordVulkanFrame(VulkanRenderState& vk, VkCommandBuffer cmd,
                                     uint32_t imageIndex, const BallMotionState& ball,
                                     uint64_t frameCounter) {
        const VkExtent2D extent = vk.swapchain.GetExtent();
        const float aspect = extent.height > 0
                ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                : 1.0f;
        const bool diverged = get_is_fps_diverged();
        const samples::Circle::Color ballColor = diverged
                ? samples::Circle::Color{1.0f, 0.35f, 0.25f}
                : samples::Circle::Color{0.0f, 0.9f, 0.75f};
        const int32_t gpuWork = get_current_gpu_workload();
        const float drawRadius = (gpuWork > 0) ? 0.65f : 0.12f;
        const float drawX = (gpuWork > 0) ? 0.0f : ball.x;
        const float drawY = (gpuWork > 0) ? 0.0f : ball.y;
        mCurrentCircleX.store(drawX);
        mCurrentCircleY.store(drawY);
        mCurrentCircleRadius.store(drawRadius);
        const std::vector<samples::Circle> circles = {{ballColor, drawRadius, drawX, drawY}};

        if (mOverlayEnabled.load()) {
            mTelemetry.CaptureSnapshot(&mTelemetrySnapshot);
            mTelemetrySnapshot.histogramMode = mTouchDrag.histogramMode.load();
            mOverlayBatcher.BeginFrame(static_cast<float>(extent.width),
                                       static_cast<float>(extent.height));
            mOverlayBatcher.BuildTelemetryHud(mTelemetrySnapshot);
        }

        vk.circleRenderer.RecordDraw(cmd, imageIndex, extent, aspect, circles, gpuWork,
                                     mOverlayEnabled.load() ? &vk.overlayRenderer : nullptr,
                                     frameCounter,
                                     mOverlayEnabled.load() ? &mOverlayBatcher : nullptr);
        return {drawX, drawY};
    }

    // Closes the pending input-latency async trace section, if any, and logs the circle
    // position it resolved to.
    void endInputLatencyTrace(const CirclePosition& drawPos) {
        const int32_t inputCookie = mTouchDrag.activeInputCookie.exchange(0);
        if (inputCookie > 0) {
            if (auto* trace = samples::Trace::getInstance()) {
                if (trace->isEnabled()) {
                    trace->endAsyncSection("SwappyTest::InputLatency", inputCookie);
                    trace->setCounter("SwappyTest::CircleX_x1000",
                                      static_cast<int64_t>(drawPos.x * 1000.0f));
                    trace->setCounter("SwappyTest::CircleY_x1000",
                                      static_cast<int64_t>(drawPos.y * 1000.0f));
                }
            }
        }
    }

    // Submits and presents the frame, then recreates the swapchain if it went out of date.
    void presentVulkanFrame(VulkanRenderState& vk, uint32_t imageIndex, VkCommandBuffer cmd,
                            bool useSwappy) {
        const VkResult presentRes =
                vk.frameManager.EndFrame(&vk.swapchain, imageIndex, cmd, useSwappy);
        if (useSwappy) {
            // For the HUD; see currentSwapIntervalNs().
            const uint64_t curIpd = SwappyVk_getSwapIntervalNS(vk.swapchain.GetSwapchain());
            if (curIpd > 0) {
                mVkCurrentSwapIntervalNs.store(curIpd);
            }
        }

        if (presentRes == VK_ERROR_OUT_OF_DATE_KHR) {
            const SurfaceSize sz = mSurfaceSize.load();
            if (sz.width > 0 && sz.height > 0 && mRunning.load()) {
                recreateVulkanSwapchain(vk, static_cast<uint32_t>(sz.width),
                                        static_cast<uint32_t>(sz.height));
            }
        }
    }

    // Seconds since the previous frame ended, replaced by a nominal 60 Hz frame when it is
    // non-positive or longer than 100 ms.
    static float clampedFrameDeltaSeconds(std::chrono::steady_clock::time_point prevFrameEnd) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - prevFrameEnd).count();
        if (dt <= 0.0f || dt > 0.1f) dt = 0.01667f;
        return dt;
    }

    // Returns the CPU render time since `renderStart` plus the last Swappy GPU time.
    float storeFrameWorkMs(std::chrono::steady_clock::time_point renderStart) {
        auto renderEnd = std::chrono::steady_clock::now();
        const float cpuMs =
                std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();
        const float gpuMs = get_last_swappy_gpu_ms();
        return cpuMs + gpuMs;
    }

    void recordFrameTelemetry(std::chrono::steady_clock::time_point& prevFrameEnd, float frameMs) {
        auto frameEnd = std::chrono::steady_clock::now();
        const uint64_t frameDurNs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(frameEnd - prevFrameEnd)
                        .count());
        mTelemetry.RecordFrame(frameDurNs, frameMs, getPacingStrategyName(),
                               getSupportedFpsFlags());
        prevFrameEnd = frameEnd;
    }

    void destroyVulkanRenderState(VulkanRenderState& vk) {
        vk.context.WaitIdle();
        vk.overlayRenderer.Destroy();
        vk.circleRenderer.Destroy();
        vk.frameManager.Destroy();
        vk.swapchain.Destroy();
        vk.context.Destroy();
    }

    void renderLoopVulkan() {
        bool attachedJvm = false;
        VulkanRenderState vk;
        vk.env = attachRenderThreadToJvm(attachedJvm);

        SwappyGL_destroy();

        if (!initVulkanRenderState(vk)) {
            detachRenderThreadFromJvm(attachedJvm);
            return;
        }

        const std::string deviceInfoSlice = buildVulkanDeviceInfoSlice(vk.context);
        ALOGI("%s", deviceInfoSlice.c_str());

        BallMotionState ball;
        auto prevFrameEnd = std::chrono::steady_clock::now();
        bool wasTracing = false;
        int prevStepIndex = -2;
        uint64_t frameCounter = 0;
        mTelemetry.Reset();

        while (mRunning.load()) {
            switch (poll_calibration_hold()) {
                case CalibrationHoldAction::kNone:
                    break;
                case CalibrationHoldAction::kDrainAndAcknowledge:
                    vk.context.WaitIdle();
                    acknowledge_calibration_hold();
                    continue;
                case CalibrationHoldAction::kRunGpuCalibration: {
                    const SurfaceSize sz = mSurfaceSize.load();
                    float aspect = sz.height > 0 ? static_cast<float>(sz.width) / sz.height : 1.0f;
                    samples::vulkan::VulkanGpuCalibrationTarget
                            target(vk.context, vk.circleRenderer, vk.swapchain.GetImageFormat(),
                                   vk.swapchain.GetExtent(), aspect, makeCalibrationCircles());
                    run_gpu_calibration(target);
                    continue;
                }
                case CalibrationHoldAction::kIdle:
                    continue;
            }

            traceDeviceInfoIfNeeded(deviceInfoSlice, frameCounter, wasTracing, prevStepIndex);
            ++frameCounter;

            uint32_t imageIndex = 0;
            VkCommandBuffer cmd = vk.frameManager.BeginFrame(&vk.swapchain, &imageIndex);
            if (cmd == VK_NULL_HANDLE) {
                const SurfaceSize sz = mSurfaceSize.load();
                if (sz.width > 0 && sz.height > 0) {
                    recreateVulkanSwapchain(vk, static_cast<uint32_t>(sz.width),
                                            static_cast<uint32_t>(sz.height));
                }
                continue;
            }

            const bool useSwappy = mSwappyEnabled.load() && vk.swappyVkReady;
            if (useSwappy) {
                SwappyVk_recordFrameStart(vk.context.GetGraphicsQueue(),
                                          vk.swapchain.GetSwapchain(), imageIndex);
            }

            run_frame_swappy_test_workload();

            if (vk.swappyVkReady) {
                applyPendingSwappyVkUpdates(vk.context.GetDevice(), vk.swapchain.GetSwapchain());
            }

            const float dt = clampedFrameDeltaSeconds(prevFrameEnd);
            updateBallMotion(ball, dt);

            auto tVkStart = std::chrono::steady_clock::now();
            const CirclePosition drawPos =
                    recordVulkanFrame(vk, cmd, imageIndex, ball, frameCounter);
            const float frameWorkMs = storeFrameWorkMs(tVkStart);

            endInputLatencyTrace(drawPos);

            presentVulkanFrame(vk, imageIndex, cmd, useSwappy);

            recordFrameTelemetry(prevFrameEnd, frameWorkMs);
        }

        destroyVulkanRenderState(vk);
        detachRenderThreadFromJvm(attachedJvm);
    }

    // GLES objects owned by the render thread for the duration of renderLoopGles().
    // Members are declared in the original construction order so that destructors run in
    // the same (reverse) order.
    struct GlesRenderState {
        samples::gles::GlesContext context;
        samples::gles::GlesCircleRenderer circleRenderer;
        samples::gles::GlesOverlayRenderer overlayRenderer;
        bool noSwappy = false;
    };

    // Creates the EGL context, SwappyGL binding and renderers. Only a context failure is
    // fatal: it stops the render loop and returns false. Renderer failures are just logged.
    bool initGlesRenderState(GlesRenderState& gl, JNIEnv* env) {
        if (!gl.context.Initialize(mWindow)) {
            ALOGE("GlesContext::Initialize failed");
            mRunning.store(false);
            return false;
        }

        // No Swappy runs never create a SwappyGL instance, so every frame
        // below takes the raw eglSwapBuffers path.
        gl.noSwappy = isNoSwappy();
        if (!gl.noSwappy) {
            initSwappyGl(env);
            SwappyGL_setWindow(mWindow);
        }

        if (!gl.circleRenderer.Initialize()) {
            ALOGE("GlesCircleRenderer::Initialize failed");
        }

        if (!gl.overlayRenderer.Initialize()) {
            ALOGE("GlesOverlayRenderer::Initialize failed");
        }
        return true;
    }

    std::string buildGlesDeviceInfoSlice(const samples::gles::GlesContext& context) {
        return buildDeviceInfoTraceSlice("gles", getPacingStrategyName(), context.GetVendor(),
                                         context.GetRenderer(), context.GetVersion());
    }

    // Builds the circle and overlay HUD for this frame and draws them at the current surface
    // size. Returns the drawn circle center.
    CirclePosition drawGlesFrame(GlesRenderState& gl, const BallMotionState& ball) {
        const SurfaceSize size = mSurfaceSize.load();
        float aspect = size.height > 0 ? static_cast<float>(size.width) / size.height : 1.0f;
        bool diverged = get_is_fps_diverged();
        samples::Circle::Color ballColor = diverged ? samples::Circle::Color{1.0f, 0.35f, 0.25f}
                                                    : samples::Circle::Color{0.0f, 0.9f, 0.75f};
        int32_t gpuWork = get_current_gpu_workload();
        const float drawRadius = (gpuWork > 0) ? 0.65f : 0.12f;
        const float drawX = (gpuWork > 0) ? 0.0f : ball.x;
        const float drawY = (gpuWork > 0) ? 0.0f : ball.y;
        const std::vector<samples::Circle> circles = {{ballColor, drawRadius, drawX, drawY}};
        mCurrentCircleX.store(drawX);
        mCurrentCircleY.store(drawY);
        mCurrentCircleRadius.store(drawRadius);

        if (mOverlayEnabled.load()) {
            mTelemetry.CaptureSnapshot(&mTelemetrySnapshot);
            mTelemetrySnapshot.histogramMode = mTouchDrag.histogramMode.load();
            mOverlayBatcher.BeginFrame(static_cast<float>(size.width),
                                       static_cast<float>(size.height));
            mOverlayBatcher.BuildTelemetryHud(mTelemetrySnapshot);
        }

        gl.circleRenderer.Draw(size.width, size.height, aspect, circles, gpuWork,
                               mOverlayEnabled.load() ? &gl.overlayRenderer : nullptr,
                               mOverlayEnabled.load() ? &mOverlayBatcher : nullptr);
        return {drawX, drawY};
    }

    void destroyGlesRenderState(GlesRenderState& gl) {
        gl.overlayRenderer.Destroy();
        gl.circleRenderer.Destroy();
        if (!gl.noSwappy) {
            SwappyGL_setWindow(nullptr);
            SwappyGL_destroy();
        }
        gl.context.Destroy();
    }

    void renderLoopGles() {
        bool attachedJvm = false;
        GlesRenderState gl;
        JNIEnv* env = attachRenderThreadToJvm(attachedJvm);

        if (!initGlesRenderState(gl, env)) {
            detachRenderThreadFromJvm(attachedJvm);
            return;
        }

        const std::string deviceInfoSlice = buildGlesDeviceInfoSlice(gl.context);
        ALOGI("%s", deviceInfoSlice.c_str());

        BallMotionState ball;
        auto prevFrameEnd = std::chrono::steady_clock::now();
        bool wasTracing = false;
        int prevStepIndex = -2;
        uint64_t frameCounter = 0;
        mTelemetry.Reset();

        while (mRunning.load()) {
            switch (poll_calibration_hold()) {
                case CalibrationHoldAction::kNone:
                    break;
                case CalibrationHoldAction::kDrainAndAcknowledge:
                    glFinish();
                    acknowledge_calibration_hold();
                    continue;
                case CalibrationHoldAction::kRunGpuCalibration: {
                    const SurfaceSize sz = mSurfaceSize.load();
                    float aspect = sz.height > 0 ? static_cast<float>(sz.width) / sz.height : 1.0f;
                    samples::gles::GlesGpuCalibrationTarget target(gl.context, gl.circleRenderer,
                                                                   sz.width, sz.height, aspect,
                                                                   makeCalibrationCircles());
                    run_gpu_calibration(target);
                    continue;
                }
                case CalibrationHoldAction::kIdle:
                    continue;
            }

            traceDeviceInfoIfNeeded(deviceInfoSlice, frameCounter, wasTracing, prevStepIndex);
            ++frameCounter;

            const bool useSwappy = !gl.noSwappy && mSwappyEnabled.load() && SwappyGL_isEnabled();
            gl.context.RecordFrameStart(useSwappy);

            // Execute calibrated DVFS workload inside the Swappy frame window
            run_frame_swappy_test_workload();

            // Update bouncyball position and color based on current test phase / divergence
            const float dt = clampedFrameDeltaSeconds(prevFrameEnd);
            updateBallMotion(ball, dt);

            auto tGlStart = std::chrono::steady_clock::now();
            const CirclePosition drawPos = drawGlesFrame(gl, ball);
            const float frameWorkMs = storeFrameWorkMs(tGlStart);

            endInputLatencyTrace(drawPos);

            gl.context.SwapBuffers(useSwappy);

            recordFrameTelemetry(prevFrameEnd, frameWorkMs);
        }

        destroyGlesRenderState(gl);
        detachRenderThreadFromJvm(attachedJvm);
    }

    struct SurfaceSize {
        int32_t width;
        int32_t height;
    };

    std::mutex mJniMutex;
    JavaVM* mJavaVM = nullptr;
    jobject mActivityGlobalRef = nullptr;
    std::atomic<bool> mVkTracerInjected{false};
    std::atomic<bool> mRenderApiExplicitlySet{false};

    ANativeWindow* mWindow = nullptr;
    // Set on the UI thread while the render loop reads it every frame, as one
    // value so that no frame pairs the new width with the old height.
    std::atomic<SurfaceSize> mSurfaceSize{SurfaceSize{0, 0}};
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mSwappyEnabled{true};
    std::atomic<bool> mAutoPipelineMode{true};
    std::atomic<bool> mAutoSwapInterval{true};
    std::atomic<uint64_t> mSwapIntervalNs{SWAPPY_SWAP_60FPS};
    std::atomic<uint64_t> mVkCurrentSwapIntervalNs{SWAPPY_SWAP_60FPS};
    std::atomic<uint64_t> mVkRefreshDurationNs{0};
    std::array<std::atomic<uint64_t>, 16> mVkSupportedRefreshPeriodsNs{};
    std::atomic<int> mVkSupportedRefreshPeriodCount{0};
    std::atomic<bool> mVkConfigDirty{true};
    std::atomic<bool> mPendingVkResetPacing{false};
    std::atomic<int32_t> mBufferStuffingFixWait{-1};
    std::atomic<int32_t> mFenceTimeoutMs{-1};
    std::atomic<bool> mSet30FpsLimit{false};
    std::atomic<int> mPacingStrategy{kPacingClassic};
    std::atomic<bool> mOverlayEnabled{true};
    samples::TelemetryRingBuffer mTelemetry;
    samples::OverlayBatcher mOverlayBatcher;
    samples::TelemetrySnapshot mTelemetrySnapshot;
    TouchDragState mTouchDrag;
    std::atomic<float> mCurrentCircleX{0.0f};
    std::atomic<float> mCurrentCircleY{0.0f};
    std::atomic<float> mCurrentCircleRadius{0.12f};
    std::atomic<int32_t> mInputCookieCounter{0};
    std::thread mRenderThread;
};

} // namespace

extern "C" {

uint64_t swappy_backend_get_refresh_period_ns(void) {
    return SwappyTestRenderer::getInstance().getRefreshDurationNs();
}

void swappy_backend_set_swap_interval_ns(uint64_t swap_ns) {
    SwappyTestRenderer::getInstance().setSwapIntervalNs(swap_ns);
}

void swappy_backend_set_auto_swap_interval(bool enabled) {
    SwappyTestRenderer::getInstance().setAutoSwapInterval(enabled);
}

void swappy_backend_set_auto_pipeline_mode(bool enabled) {
    SwappyTestRenderer::getInstance().setAutoPipelineFlagOnly(enabled);
}

void swappy_backend_set_buffer_stuffing_fix_wait(int32_t n_frames) {
    SwappyTestRenderer::getInstance().setBufferStuffingFixWait(n_frames);
}

void swappy_backend_reset_frame_pacing(void) {
    SwappyTestRenderer::getInstance().resetFramePacing();
}

const char* swappy_backend_get_pacing_strategy_name(void) {
    return SwappyTestRenderer::getInstance().getPacingStrategyName();
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nInit(JNIEnv* env,
                                                                            jobject activity) {
    SwappyTestRenderer::getInstance().init(env, activity);
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nSetRenderApi(
        JNIEnv* /* env */, jobject /* this */, jint api) {
    SwappyTestRenderer::getInstance().setRenderApi(api == 1 ? SWAPPY_RENDER_API_VULKAN
                                                            : SWAPPY_RENDER_API_GLES);
}

JNIEXPORT jint JNICALL Java_com_samples_swappytest_SwappyTestActivity_nGetRenderApi(
        JNIEnv* /* env */, jobject /* this */) {
    return static_cast<jint>(SwappyTestRenderer::getInstance().getRenderApi());
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nSetOptions(
        JNIEnv* /* env */, jobject /* this */, jboolean swappyEnabled, jboolean set30FpsLimit) {
    SwappyTestRenderer::getInstance().setOptions(swappyEnabled, set30FpsLimit);
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nSetPipelineMode(
        JNIEnv* env, jobject /* this */, jstring mode) {
    if (!mode) {
        SwappyTestRenderer::getInstance().setPipelineMode("auto");
        return;
    }
    const char* str = env->GetStringUTFChars(mode, nullptr);
    SwappyTestRenderer::getInstance().setPipelineMode(str);
    env->ReleaseStringUTFChars(mode, str);
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nSetAutoSwapInterval(
        JNIEnv* /* env */, jobject /* this */, jboolean enabled) {
    SwappyTestRenderer::getInstance().setAutoSwapInterval(enabled);
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nSetBufferStuffingFixWait(
        JNIEnv* /* env */, jobject /* this */, jint nFrames) {
    SwappyTestRenderer::getInstance().setBufferStuffingFixWait(nFrames);
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nSetFenceTimeoutMs(
        JNIEnv* /* env */, jobject /* this */, jint timeoutMs) {
    SwappyTestRenderer::getInstance().setFenceTimeoutMs(timeoutMs);
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nSetPacingStrategy(
        JNIEnv* /* env */, jobject /* this */, jint strategy) {
    SwappyTestRenderer::getInstance().setPacingStrategy(static_cast<int>(strategy));
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nStartRender(
        JNIEnv* env, jobject /* this */, jobject surface, jint width, jint height,
        jboolean autoStart) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    SwappyTestRenderer::getInstance().start(window, width, height, autoStart);
}

JNIEXPORT void JNICALL
Java_com_samples_swappytest_SwappyTestActivity_nStopRender(JNIEnv* /* env */, jobject /* this */) {
    SwappyTestRenderer::getInstance().stop();
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nSetSurfaceSize(
        JNIEnv* /* env */, jobject /* this */, jint width, jint height) {
    SwappyTestRenderer::getInstance().setSurfaceSize(width, height);
}

// Fills the SwappyTestActivity.UiSnapshot `out` from get_ui_snapshot().
JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nGetUiSnapshot(
        JNIEnv* env, jobject /* this */, jobject out) {
    DvfsUiSnapshot ui;
    get_ui_snapshot(&ui);
    jclass cls = env->GetObjectClass(out);
    env->SetIntField(out, env->GetFieldID(cls, "testPhase", "I"), ui.test_phase);
    env->SetBooleanField(out, env->GetFieldID(cls, "runTainted", "Z"), ui.run_tainted);
    env->SetBooleanField(out, env->GetFieldID(cls, "runFailed", "Z"), ui.run_failed);
    env->SetObjectField(out, env->GetFieldID(cls, "taintReason", "Ljava/lang/String;"),
                        env->NewStringUTF(ui.taint_reason));
    env->SetObjectField(out, env->GetFieldID(cls, "testScenario", "Ljava/lang/String;"),
                        env->NewStringUTF(ui.test_scenario));
    jintArray status = static_cast<jintArray>(
            env->GetObjectField(out, env->GetFieldID(cls, "calibrationStatus", "[I")));
    jint status_values[CALIBRATION_KIND_COUNT];
    for (int k = 0; k < CALIBRATION_KIND_COUNT; ++k) status_values[k] = ui.calibration_status[k];
    env->SetIntArrayRegion(status, 0, CALIBRATION_KIND_COUNT, status_values);
    env->SetIntField(out, env->GetFieldID(cls, "missingCalibrations", "I"),
                     static_cast<jint>(ui.missing_calibrations));
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nSetTestScenario(
        JNIEnv* env, jobject /* this */, jstring scenario) {
    if (!scenario) {
        set_test_scenario("S1");
        return;
    }
    const char* str = env->GetStringUTFChars(scenario, nullptr);
    set_test_scenario(str);
    env->ReleaseStringUTFChars(scenario, str);
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nSetCustomSteps(
        JNIEnv* env, jobject /* this */, jstring stepsSpec) {
    if (!stepsSpec) {
        set_custom_steps("");
        return;
    }
    const char* str = env->GetStringUTFChars(stepsSpec, nullptr);
    set_custom_steps(str);
    env->ReleaseStringUTFChars(stepsSpec, str);
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nSetRunIteration(
        JNIEnv* /* env */, jobject /* this */, jint iter) {
    set_run_iteration(iter);
}

JNIEXPORT jint JNICALL Java_com_samples_swappytest_SwappyTestActivity_nGetScenarioCount(
        JNIEnv* /* env */, jobject /* this */) {
    return get_scenario_count();
}

JNIEXPORT jstring JNICALL Java_com_samples_swappytest_SwappyTestActivity_nGetScenarioId(
        JNIEnv* env, jobject /* this */, jint scenarioIndex) {
    const char* id = get_scenario_id_by_index(scenarioIndex);
    return id ? env->NewStringUTF(id) : nullptr;
}

JNIEXPORT jstring JNICALL Java_com_samples_swappytest_SwappyTestActivity_nGetScenarioDescription(
        JNIEnv* env, jobject /* this */, jint scenarioIndex) {
    const char* desc = get_scenario_description_by_index(scenarioIndex);
    return desc ? env->NewStringUTF(desc) : nullptr;
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nSetOutputDirectory(
        JNIEnv* env, jobject /* this */, jstring dirPath) {
    if (!dirPath) return;
    const char* str = env->GetStringUTFChars(dirPath, nullptr);
    set_output_directory(str);
    env->ReleaseStringUTFChars(dirPath, str);
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nSetCalibrationRun(
        JNIEnv* /* env */, jobject /* this */, jint kinds) {
    set_calibration_run(static_cast<CalibrationSet>(kinds));
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nRefreshCalibrationStatus(
        JNIEnv* /* env */, jobject /* this */) {
    refresh_calibration_status();
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nSetOverlayEnabled(
        JNIEnv* /* env */, jobject /* this */, jboolean enabled) {
    SwappyTestRenderer::getInstance().setOverlayEnabled(enabled);
}

JNIEXPORT void JNICALL Java_com_samples_swappytest_SwappyTestActivity_nOnTouchEvent(
        JNIEnv* /* env */, jobject /* this */, jint action, jfloat x, jfloat y, jlong eventTimeNs,
        jboolean isManual) {
    if (action == 0) {
        SwappyTestRenderer::getInstance().onPointerDown(x, y, eventTimeNs, isManual);
    } else if (action == 2) {
        SwappyTestRenderer::getInstance().onPointerMove(x, y, eventTimeNs);
    } else if (action == 1 || action == 3) {
        SwappyTestRenderer::getInstance().onPointerUp(x, y, eventTimeNs);
    }
}

} // extern "C"
