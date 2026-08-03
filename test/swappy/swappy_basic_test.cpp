#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <media/NdkImageReader.h>
#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "swappy/swappyGL.h"
#include "swappy/swappyGL_extra.h"
#include "swappy/swappyVk.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "swappy_test", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "swappy_test", __VA_ARGS__)

#define VK_CHECK(result) ASSERT_EQ(VK_SUCCESS, result)

#ifdef NATIVE_COVERAGE
#include <android/log.h>
#include <stdlib.h>
extern "C" int __llvm_profile_write_file(void);
extern "C" void __llvm_profile_set_filename(const char*);
extern "C" void Swappy_dumpCoverage();

class CoverageEnvironment : public ::testing::Environment {
public:
    ~CoverageEnvironment() override {}
    void TearDown() override {
        const char* coverage_dir = getenv("COVERAGE_DIR");
        std::string path = coverage_dir
                ? std::string(coverage_dir) + "/swappy_coverage.profraw"
                : "/data/data/com.swappy.testapp/cache/swappy_coverage.profraw";
        __llvm_profile_set_filename(path.c_str());
        int ret = __llvm_profile_write_file();
        __android_log_print(ANDROID_LOG_INFO, "SwappyTest", "LLVM profile write returned %d", ret);
    }
};
static const ::testing::Environment* const coverage_env =
        ::testing::AddGlobalTestEnvironment(new CoverageEnvironment);
#endif

namespace android {

namespace swappytest {

class MockJNI {
public:
    MOCK_METHOD(jclass, FindClass, (JNIEnv*, const char*));
    MOCK_METHOD(jmethodID, GetMethodID, (JNIEnv*, jclass, const char*, const char*));
    MOCK_METHOD(jfieldID, GetStaticFieldID, (JNIEnv*, jclass, const char*, const char*));
    MOCK_METHOD(jint, GetStaticIntField, (JNIEnv*, jclass, jfieldID));
    MOCK_METHOD(jobject, CallObjectMethodV, (JNIEnv*, jobject, jmethodID, va_list));
    MOCK_METHOD(jfloat, CallFloatMethodV, (JNIEnv*, jobject, jmethodID, va_list));
    MOCK_METHOD(jlong, CallLongMethodV, (JNIEnv*, jobject, jmethodID, va_list));
    MOCK_METHOD(jboolean, ExceptionCheck, (JNIEnv*));
    MOCK_METHOD(jthrowable, ExceptionOccurred, (JNIEnv*));
    MOCK_METHOD(void, ExceptionClear, (JNIEnv*));
    MOCK_METHOD(jint, GetJavaVM, (JNIEnv*, JavaVM**));
    MOCK_METHOD(jobject, NewGlobalRef, (JNIEnv*, jobject));
    MOCK_METHOD(void, DeleteGlobalRef, (JNIEnv*, jobject));
    MOCK_METHOD(jobject, GetStaticObjectField, (JNIEnv*, jclass, jfieldID));
    MOCK_METHOD(const char*, GetStringUTFChars, (JNIEnv*, jstring, jboolean*));
    MOCK_METHOD(jsize, GetStringUTFLength, (JNIEnv*, jstring));
    MOCK_METHOD(void, ReleaseStringUTFChars, (JNIEnv*, jstring, const char*));
    MOCK_METHOD(void, DeleteLocalRef, (JNIEnv*, jobject));
    MOCK_METHOD(jclass, GetObjectClass, (JNIEnv*, jobject));
    MOCK_METHOD(jstring, NewStringUTF, (JNIEnv*, const char*));
    MOCK_METHOD(jobject, NewObjectV, (JNIEnv*, jclass, jmethodID, va_list));
    MOCK_METHOD(jint, RegisterNatives, (JNIEnv*, jclass, const JNINativeMethod*, jint));
    MOCK_METHOD(void, CallVoidMethodV, (JNIEnv*, jobject, jmethodID, va_list));
};

static MockJNI* gMockJni = nullptr;

static jclass MockFindClass(JNIEnv* env, const char* name) {
    return gMockJni->FindClass(env, name);
}
static jmethodID MockGetMethodID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    return gMockJni->GetMethodID(env, clazz, name, sig);
}
static jfieldID MockGetStaticFieldID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
    return gMockJni->GetStaticFieldID(env, clazz, name, sig);
}
static jint MockGetStaticIntField(JNIEnv* env, jclass clazz, jfieldID fieldID) {
    return gMockJni->GetStaticIntField(env, clazz, fieldID);
}
static jobject MockCallObjectMethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) {
    return gMockJni->CallObjectMethodV(env, obj, methodID, args);
}
static jfloat MockCallFloatMethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) {
    return gMockJni->CallFloatMethodV(env, obj, methodID, args);
}
static jlong MockCallLongMethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) {
    return gMockJni->CallLongMethodV(env, obj, methodID, args);
}
static void MockCallVoidMethodV(JNIEnv* env, jobject obj, jmethodID methodID, va_list args) {
    gMockJni->CallVoidMethodV(env, obj, methodID, args);
}
static jboolean MockExceptionCheck(JNIEnv* env) {
    return gMockJni->ExceptionCheck(env);
}
static jthrowable MockExceptionOccurred(JNIEnv* env) {
    return gMockJni->ExceptionOccurred(env);
}
static void MockExceptionClear(JNIEnv* env) {
    gMockJni->ExceptionClear(env);
}
static jint MockGetJavaVM(JNIEnv* env, JavaVM** vm) {
    return gMockJni->GetJavaVM(env, vm);
}
static jobject MockNewGlobalRef(JNIEnv* env, jobject obj) {
    return gMockJni->NewGlobalRef(env, obj);
}
static void MockDeleteGlobalRef(JNIEnv* env, jobject obj) {
    gMockJni->DeleteGlobalRef(env, obj);
}
static jobject MockGetStaticObjectField(JNIEnv* env, jclass clazz, jfieldID fieldID) {
    return gMockJni->GetStaticObjectField(env, clazz, fieldID);
}
static const char* MockGetStringUTFChars(JNIEnv* env, jstring string, jboolean* isCopy) {
    return gMockJni->GetStringUTFChars(env, string, isCopy);
}
static jsize MockGetStringUTFLength(JNIEnv* env, jstring string) {
    return gMockJni->GetStringUTFLength(env, string);
}
static void MockReleaseStringUTFChars(JNIEnv* env, jstring string, const char* utf) {
    gMockJni->ReleaseStringUTFChars(env, string, utf);
}
static void MockDeleteLocalRef(JNIEnv* env, jobject localRef) {
    gMockJni->DeleteLocalRef(env, localRef);
}
static jclass MockGetObjectClass(JNIEnv* env, jobject obj) {
    return gMockJni->GetObjectClass(env, obj);
}
static jstring MockNewStringUTF(JNIEnv* env, const char* bytes) {
    return gMockJni->NewStringUTF(env, bytes);
}
static jobject MockNewObjectV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
    return gMockJni->NewObjectV(env, clazz, methodID, args);
}
static jint MockRegisterNatives(JNIEnv* env, jclass clazz, const JNINativeMethod* methods,
                                jint nMethods) {
    return gMockJni->RegisterNatives(env, clazz, methods, nMethods);
}

static _JNIEnv* gMockEnv = nullptr;
static std::mutex gTestSerializationMutex;

static jint MockAttachCurrentThread(JavaVM*, JNIEnv** env, void*) {
    if (env && gMockEnv) {
        *env = gMockEnv;
    }
    return JNI_OK;
}

static jint MockGetEnv(JavaVM*, void** env, jint) {
    if (env && gMockEnv) {
        *env = gMockEnv;
    }
    return JNI_OK;
}

static jint MockDetachCurrentThread(JavaVM*) {
    return JNI_OK;
}

extern "C" JNIEXPORT void JNICALL
Java_com_google_androidgamesdk_ChoreographerCallback_nOnChoreographer(JNIEnv* env, jobject thisObj,
                                                                      jlong cookie,
                                                                      jlong frameTimeNanos);

class AImageReaderSwapchainTestBase : public ::testing::Test {
private:
    std::atomic<jlong> mChoreographerCookie{0};
    std::atomic<int> mRunningThreads{0};
    std::vector<std::thread> mMockThreads;
    std::mutex mMockThreadsMutex;

public:
    AImageReaderSwapchainTestBase() {
        gTestSerializationMutex.lock();
    }

    virtual ~AImageReaderSwapchainTestBase() {
        gTestSerializationMutex.unlock();
    }

    AImageReader* mReader = nullptr;
    ANativeWindow* mWindow = nullptr;

    testing::NiceMock<MockJNI> mMockJni;
    _JNIEnv mEnvStruct;
    JNIInvokeInterface mInvokeInterface = {};
    _JavaVM mJavaVM;
    JNINativeInterface mJniInterface = {};

    void setChoreographerCookie(va_list args) {
        va_list args_copy;
        va_copy(args_copy, args);
        mChoreographerCookie = va_arg(args_copy, jlong);
        va_end(args_copy);
    }

    void spawnMockChoreographerThread() {
        mRunningThreads++;
        std::lock_guard<std::mutex> lock(mMockThreadsMutex);
        mMockThreads.emplace_back([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            jlong cookie = mChoreographerCookie.load();
            if (cookie != 0) {
                auto now = std::chrono::steady_clock::now().time_since_epoch();
                jlong timeNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
                Java_com_google_androidgamesdk_ChoreographerCallback_nOnChoreographer(nullptr,
                                                                                      nullptr,
                                                                                      cookie,
                                                                                      timeNanos);
            }
            mRunningThreads--;
        });
    }

    void setupMockChoreographer() {
        // Override JNI Mock for <init> and postFrameCallback to intercept Choreographer callbacks.
        ON_CALL(mMockJni, GetMethodID(testing::_, testing::_, testing::StrEq("<init>"), testing::_))
                .WillByDefault(testing::Return(reinterpret_cast<jmethodID>(0x100)));
        ON_CALL(mMockJni,
                GetMethodID(testing::_, testing::_, testing::StrEq("postFrameCallback"),
                            testing::_))
                .WillByDefault(testing::Return(reinterpret_cast<jmethodID>(0x101)));

        // When Swappy calls into the choreographer, grab the cookie for our mock thread.
        ON_CALL(mMockJni, NewObjectV(testing::_, testing::_, testing::_, testing::_))
                .WillByDefault(
                        testing::Invoke([this](JNIEnv*, jclass, jmethodID methodID, va_list args) {
                            if (methodID == reinterpret_cast<jmethodID>(0x100)) {
                                setChoreographerCookie(args);
                            }
                            return reinterpret_cast<jobject>(0x8);
                        }));

        // Spin up a mock thread to simulate the Choreographer firing callbacks on frame events.
        ON_CALL(mMockJni, CallVoidMethodV(testing::_, testing::_, testing::_, testing::_))
                .WillByDefault(testing::Invoke(
                        [this](JNIEnv* env, jobject obj, jmethodID methodID, va_list) {
                            if (methodID == reinterpret_cast<jmethodID>(0x101)) {
                                spawnMockChoreographerThread();
                            }
                        }));
    }

    void cleanupMockChoreographer() {
        mChoreographerCookie = 0;
        while (true) {
            std::vector<std::thread> toJoin;
            {
                std::lock_guard<std::mutex> lock(mMockThreadsMutex);
                toJoin = std::move(mMockThreads);
            }
            if (toJoin.empty() && mRunningThreads.load() == 0) {
                break;
            }
            for (auto& t : toJoin) {
                if (t.joinable()) {
                    t.join();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void TearDown() override {
        if (mReader) {
            AImageReader_delete(mReader);
            mReader = nullptr;
        }
        mWindow = nullptr;
        cleanupMockChoreographer();
        teardownMockJni();
    }

    // Sets up a comprehensive mocked JNI environment that simulates the Android
    // Java runtime structure expected by Swappy during its initialization and lifecycle.
    // This allows testing the C++ logic of Swappy completely isolated from the Java layers,
    // intercepting expected calls like retrieving the SDK version or presentation deadlines.
    // Individual tests can still append their own specific mock behaviors (via ON_CALL overrides)
    // after calling this method.
    void setupMockJni(int sdkVersion) {
        gMockJni = &mMockJni;

        ON_CALL(mMockJni, FindClass(testing::_, testing::_))
                .WillByDefault(testing::Return(reinterpret_cast<jclass>(0x1)));
        ON_CALL(mMockJni, ExceptionCheck(testing::_)).WillByDefault(testing::Return(JNI_FALSE));
        ON_CALL(mMockJni, ExceptionOccurred(testing::_)).WillByDefault(testing::Return(nullptr));
        ON_CALL(mMockJni,
                GetStaticFieldID(testing::_, testing::_, testing::StrEq("SDK_INT"), testing::_))
                .WillByDefault(testing::Return(reinterpret_cast<jfieldID>(0x1)));
        ON_CALL(mMockJni,
                GetStaticFieldID(testing::_, testing::_, testing::StrEq("PREVIEW_SDK_INT"),
                                 testing::_))
                .WillByDefault(testing::Return(reinterpret_cast<jfieldID>(0x2)));
        ON_CALL(mMockJni,
                GetStaticIntField(testing::_, testing::_, reinterpret_cast<jfieldID>(0x1)))
                .WillByDefault(testing::Return(sdkVersion));
        ON_CALL(mMockJni,
                GetStaticIntField(testing::_, testing::_, reinterpret_cast<jfieldID>(0x2)))
                .WillByDefault(testing::Return(0));

        ON_CALL(mMockJni,
                GetMethodID(testing::_, testing::_, testing::Not(testing::EndsWith("Nanos")),
                            testing::_))
                .WillByDefault(testing::Return(reinterpret_cast<jmethodID>(0x3)));
        ON_CALL(mMockJni,
                GetMethodID(testing::_, testing::_, testing::StrEq("getAppVsyncOffsetNanos"),
                            testing::_))
                .WillByDefault(testing::Return(reinterpret_cast<jmethodID>(0x10)));
        ON_CALL(mMockJni,
                GetMethodID(testing::_, testing::_, testing::StrEq("getPresentationDeadlineNanos"),
                            testing::_))
                .WillByDefault(testing::Return(reinterpret_cast<jmethodID>(0x11)));

        ON_CALL(mMockJni, CallObjectMethodV(testing::_, testing::_, testing::_, testing::_))
                .WillByDefault(testing::Return(reinterpret_cast<jobject>(0x4)));
        ON_CALL(mMockJni, CallFloatMethodV(testing::_, testing::_, testing::_, testing::_))
                .WillByDefault(testing::Return(60.0f));

        ON_CALL(mMockJni,
                CallLongMethodV(testing::_, testing::_, reinterpret_cast<jmethodID>(0x10),
                                testing::_))
                .WillByDefault(testing::Return(1000000));
        ON_CALL(mMockJni,
                CallLongMethodV(testing::_, testing::_, reinterpret_cast<jmethodID>(0x11),
                                testing::_))
                .WillByDefault(testing::Return(16666666));

        mInvokeInterface.AttachCurrentThread = MockAttachCurrentThread;
        mInvokeInterface.DetachCurrentThread = MockDetachCurrentThread;
        mInvokeInterface.GetEnv = MockGetEnv;

        mJavaVM.functions = &mInvokeInterface;

        ON_CALL(mMockJni, GetJavaVM(testing::_, testing::_))
                .WillByDefault(testing::DoAll(testing::SetArgPointee<1>(&mJavaVM),
                                              testing::Return(JNI_OK)));
        ON_CALL(mMockJni, NewGlobalRef(testing::_, testing::_))
                .WillByDefault(testing::ReturnArg<1>());
        ON_CALL(mMockJni, GetStaticObjectField(testing::_, testing::_, testing::_))
                .WillByDefault(testing::Return(reinterpret_cast<jobject>(0x5)));
        ON_CALL(mMockJni, GetStringUTFChars(testing::_, testing::_, testing::_))
                .WillByDefault(testing::Return(""));
        ON_CALL(mMockJni, GetStringUTFLength(testing::_, testing::_))
                .WillByDefault(testing::Return(0));
        ON_CALL(mMockJni, GetObjectClass(testing::_, testing::_))
                .WillByDefault(testing::Return(reinterpret_cast<jclass>(0x6)));
        ON_CALL(mMockJni, NewStringUTF(testing::_, testing::_))
                .WillByDefault(testing::Return(reinterpret_cast<jstring>(0x7)));
        ON_CALL(mMockJni, NewObjectV(testing::_, testing::_, testing::_, testing::_))
                .WillByDefault(testing::Return(reinterpret_cast<jobject>(0x8)));
        ON_CALL(mMockJni, CallVoidMethodV(testing::_, testing::_, testing::_, testing::_))
                .WillByDefault(testing::Invoke([](JNIEnv*, jobject, jmethodID, va_list) {}));
        ON_CALL(mMockJni, RegisterNatives(testing::_, testing::_, testing::_, testing::_))
                .WillByDefault(testing::Return(JNI_OK));

        mJniInterface.FindClass = MockFindClass;
        mJniInterface.GetMethodID = MockGetMethodID;
        mJniInterface.GetStaticFieldID = MockGetStaticFieldID;
        mJniInterface.GetStaticIntField = MockGetStaticIntField;
        mJniInterface.CallObjectMethodV = MockCallObjectMethodV;
        mJniInterface.CallFloatMethodV = MockCallFloatMethodV;
        mJniInterface.CallLongMethodV = MockCallLongMethodV;
        mJniInterface.CallVoidMethodV = MockCallVoidMethodV;
        mJniInterface.ExceptionCheck = MockExceptionCheck;
        mJniInterface.ExceptionOccurred = MockExceptionOccurred;
        mJniInterface.ExceptionClear = MockExceptionClear;
        mJniInterface.GetJavaVM = MockGetJavaVM;
        mJniInterface.NewGlobalRef = MockNewGlobalRef;
        mJniInterface.DeleteGlobalRef = MockDeleteGlobalRef;
        mJniInterface.GetStaticObjectField = MockGetStaticObjectField;
        mJniInterface.GetStringUTFChars = MockGetStringUTFChars;
        mJniInterface.GetStringUTFLength = MockGetStringUTFLength;
        mJniInterface.ReleaseStringUTFChars = MockReleaseStringUTFChars;
        mJniInterface.DeleteLocalRef = MockDeleteLocalRef;
        mJniInterface.GetObjectClass = MockGetObjectClass;
        mJniInterface.NewStringUTF = MockNewStringUTF;
        mJniInterface.NewObjectV = MockNewObjectV;
        mJniInterface.RegisterNatives = MockRegisterNatives;

        mEnvStruct.functions = &mJniInterface;
        gMockEnv = &mEnvStruct;
    }

    void teardownMockJni() {
        gMockJni = nullptr;
        gMockEnv = nullptr;
    }

    // ------------------------------------------------------
    // Helper methods
    // ------------------------------------------------------

    void createAImageReader(int width, int height, int format, int maxImages,
                            AImageReader_ImageListener* listener = nullptr) {
        media_status_t status = AImageReader_new(width, height, format, maxImages, &mReader);
        ASSERT_EQ(AMEDIA_OK, status) << "Failed to create AImageReader";
        ASSERT_NE(nullptr, mReader) << "AImageReader is null";

        LOGI("AImageReader created with %dx%d, format=%d", width, height, format);
        if (listener) {
            AImageReader_setImageListener(mReader, listener);
        }
    }

    void getANativeWindowFromReader() {
        ASSERT_NE(nullptr, mReader);

        media_status_t status = AImageReader_getWindow(mReader, &mWindow);
        ASSERT_EQ(AMEDIA_OK, status) << "Failed to get ANativeWindow from AImageReader";
        ASSERT_NE(nullptr, mWindow) << "ANativeWindow is null";
        LOGI("ANativeWindow obtained from AImageReader");
    }

    static void onImageAvailable(void*, AImageReader* reader) {
        LOGI("onImageAvailable callback triggered");
        AImage* image = nullptr;
        media_status_t status = AImageReader_acquireLatestImage(reader, &image);
        if (status != AMEDIA_OK || !image) {
            LOGE("Failed to acquire latest image");
            return;
        }
        AImage_delete(image);
        LOGI("Released acquired image");
    }
};

class AImageReaderVulkanSwapchainTest : public AImageReaderSwapchainTestBase {
public:
    VkInstance mVkInstance = VK_NULL_HANDLE;
    VkPhysicalDevice mPhysicalDev = VK_NULL_HANDLE;
    VkDevice mDevice = VK_NULL_HANDLE;
    VkSurfaceKHR mSurface = VK_NULL_HANDLE;
    VkQueue mPresentQueue = VK_NULL_HANDLE;
    uint32_t mPresentQueueFamily = UINT32_MAX;
    VkSwapchainKHR mSwapchain = VK_NULL_HANDLE;

    void createVulkanInstance(std::vector<const char*>& layers) {
        const char* extensions[] = {
                VK_KHR_SURFACE_EXTENSION_NAME,
                VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
                VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
                VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
        };

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "AImageReader Vulkan Swapchain Test";
        appInfo.applicationVersion = 1;
        appInfo.pEngineName = "TestEngine";
        appInfo.engineVersion = 1;
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo instInfo{};
        instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instInfo.pApplicationInfo = &appInfo;
        instInfo.enabledExtensionCount =
                static_cast<uint32_t>(sizeof(extensions) / sizeof(extensions[0]));
        instInfo.ppEnabledExtensionNames = extensions;
        instInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
        instInfo.ppEnabledLayerNames = layers.data();
        VkResult res = vkCreateInstance(&instInfo, nullptr, &mVkInstance);
        VK_CHECK(res);
        LOGI("Vulkan instance created");
    }

    void createVulkanSurface() {
        ASSERT_NE((VkInstance)VK_NULL_HANDLE, mVkInstance);
        ASSERT_NE((ANativeWindow*)nullptr, mWindow);

        VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo{};
        surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        surfaceCreateInfo.window = mWindow;

        VkResult res =
                vkCreateAndroidSurfaceKHR(mVkInstance, &surfaceCreateInfo, nullptr, &mSurface);
        VK_CHECK(res);
        LOGI("Vulkan surface created from ANativeWindow");
    }

    void pickPhysicalDeviceAndQueueFamily() {
        ASSERT_NE((VkInstance)VK_NULL_HANDLE, mVkInstance);

        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(mVkInstance, &deviceCount, nullptr);
        ASSERT_GT(deviceCount, 0U) << "No Vulkan physical devices found!";

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(mVkInstance, &deviceCount, devices.data());

        for (auto& dev : devices) {
            uint32_t queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);
            std::vector<VkQueueFamilyProperties> queueProps(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, queueProps.data());

            for (uint32_t i = 0; i < queueFamilyCount; i++) {
                VkBool32 support = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, mSurface, &support);
                if (support == VK_TRUE) {
                    // Found a queue family that can present
                    mPhysicalDev = dev;
                    mPresentQueueFamily = i;

                    LOGI("Physical device found with queue family %u supporting "
                         "present",
                         i);
                    return;
                }
            }
        }

        FAIL() << "No physical device found that supports present to the surface!";
    }

    void createDeviceAndGetQueue(std::vector<const char*>& layers,
                                 std::vector<const char*> inExtensions = {},
                                 void* pNext = nullptr) {
        ASSERT_NE((void*)VK_NULL_HANDLE, mPhysicalDev);
        ASSERT_NE(UINT32_MAX, mPresentQueueFamily);

        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = mPresentQueueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;

        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.pNext = pNext;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
        deviceInfo.ppEnabledLayerNames = layers.data();

        std::vector<const char*> extensions = {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };
        for (auto extension : inExtensions) {
            extensions.push_back(extension);
        }
        deviceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        deviceInfo.ppEnabledExtensionNames = extensions.data();

        VkResult res = vkCreateDevice(mPhysicalDev, &deviceInfo, nullptr, &mDevice);
        VK_CHECK(res);
        LOGI("Logical device created");

        vkGetDeviceQueue(mDevice, mPresentQueueFamily, 0, &mPresentQueue);
        ASSERT_NE((VkQueue)VK_NULL_HANDLE, mPresentQueue);
        LOGI("Acquired present-capable queue");
    }

    void createSwapchain() {
        ASSERT_NE((VkDevice)VK_NULL_HANDLE, mDevice);
        ASSERT_NE((VkSurfaceKHR)VK_NULL_HANDLE, mSurface);

        VkSurfaceCapabilitiesKHR surfaceCaps{};
        VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mPhysicalDev, mSurface, &surfaceCaps));

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(mPhysicalDev, mSurface, &formatCount, nullptr);
        ASSERT_GT(formatCount, 0U);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(mPhysicalDev, mSurface, &formatCount, formats.data());

        VkSurfaceFormatKHR chosenFormat = formats[0];
        LOGI("Chosen surface format: %d", chosenFormat.format);

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(mPhysicalDev, mSurface, &presentModeCount,
                                                  nullptr);
        ASSERT_GT(presentModeCount, 0U);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(mPhysicalDev, mSurface, &presentModeCount,
                                                  presentModes.data());

        VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
        for (auto mode : presentModes) {
            if (mode == VK_PRESENT_MODE_FIFO_KHR) {
                chosenPresentMode = mode;
                break;
            }
        }

        VkExtent2D swapchainExtent{};
        if (surfaceCaps.currentExtent.width == 0xFFFFFFFF) {
            swapchainExtent.width = 640;  // fallback
            swapchainExtent.height = 480; // fallback
        } else {
            swapchainExtent = surfaceCaps.currentExtent;
        }

        uint32_t desiredImageCount = surfaceCaps.minImageCount + 1;
        if (surfaceCaps.maxImageCount > 0 && desiredImageCount > surfaceCaps.maxImageCount) {
            desiredImageCount = surfaceCaps.maxImageCount;
        }

        VkSwapchainCreateInfoKHR swapchainInfo{};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = mSurface;
        swapchainInfo.minImageCount = desiredImageCount;
        swapchainInfo.imageFormat = chosenFormat.format;
        swapchainInfo.imageColorSpace = chosenFormat.colorSpace;
        swapchainInfo.imageExtent = swapchainExtent;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage =
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        swapchainInfo.preTransform = surfaceCaps.currentTransform;
        swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        swapchainInfo.presentMode = chosenPresentMode;
        swapchainInfo.clipped = VK_TRUE;
        swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

        uint32_t queueFamilyIndices[] = {mPresentQueueFamily};
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainInfo.queueFamilyIndexCount = 1;
        swapchainInfo.pQueueFamilyIndices = queueFamilyIndices;

        VkResult res = vkCreateSwapchainKHR(mDevice, &swapchainInfo, nullptr, &mSwapchain);
        if (res == VK_SUCCESS) {
            LOGI("Swapchain created successfully");

            uint32_t swapchainImageCount = 0;
            vkGetSwapchainImagesKHR(mDevice, mSwapchain, &swapchainImageCount, nullptr);
            std::vector<VkImage> swapchainImages(swapchainImageCount);
            vkGetSwapchainImagesKHR(mDevice, mSwapchain, &swapchainImageCount,
                                    swapchainImages.data());
        } else {
            LOGI("Swapchain creation failed");
        }
    }

    void cleanUpSwapchainForTest() {
        if (mSwapchain != VK_NULL_HANDLE) {
            SwappyVk_destroySwapchain(mDevice, mSwapchain);
            vkDestroySwapchainKHR(mDevice, mSwapchain, nullptr);
            mSwapchain = VK_NULL_HANDLE;
        }
        if (mDevice != VK_NULL_HANDLE) {
            vkDestroyDevice(mDevice, nullptr);
            mDevice = VK_NULL_HANDLE;
        }
        if (mSurface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(mVkInstance, mSurface, nullptr);
            mSurface = VK_NULL_HANDLE;
        }
        if (mVkInstance != VK_NULL_HANDLE) {
            vkDestroyInstance(mVkInstance, nullptr);
            mVkInstance = VK_NULL_HANDLE;
        }
    }

    void buildSwapchainForTest(std::vector<const char*>& instanceLayers,
                               std::vector<const char*>& deviceLayers) {
        createVulkanInstance(instanceLayers);
        AImageReader_ImageListener listener{};
        listener.context = this;
        listener.onImageAvailable = &AImageReaderSwapchainTestBase::onImageAvailable;
        createAImageReader(640, 480, AIMAGE_FORMAT_PRIVATE, 3, &listener);
        getANativeWindowFromReader();
        createVulkanSurface();
        pickPhysicalDeviceAndQueueFamily();

        createDeviceAndGetQueue(deviceLayers);
        createSwapchain();
    }
};

TEST_F(AImageReaderVulkanSwapchainTest, TestHelperMethods) {
    // Verify that the basic plumbing/helper functions of these tests is
    // working. This doesn't directly test any of the layer code. It only
    // verifies that we can successfully create a swapchain with an AImageReader

    std::vector<const char*> instanceLayers;
    std::vector<const char*> deviceLayers;
    buildSwapchainForTest(instanceLayers, deviceLayers);

    ASSERT_NE(mVkInstance, (VkInstance)VK_NULL_HANDLE);
    ASSERT_NE(mPhysicalDev, (VkPhysicalDevice)VK_NULL_HANDLE);
    ASSERT_NE(mDevice, (VkDevice)VK_NULL_HANDLE);
    ASSERT_NE(mSurface, (VkSurfaceKHR)VK_NULL_HANDLE);
    ASSERT_NE(mSwapchain, (VkSwapchainKHR)VK_NULL_HANDLE);
    cleanUpSwapchainForTest();
}

TEST_F(AImageReaderVulkanSwapchainTest, Initialization) {
    // Set up the mocked JNI environment with an SDK version of 37.
    // This allows us to simulate the Android runtime environment for Swappy.
    setupMockJni(37);

    jobject fakeActivity = reinterpret_cast<jobject>(0x1234);

    // Build swapchain to provide a valid device and swapchain to Swappy
    std::vector<const char*> instanceLayers;
    std::vector<const char*> deviceLayers;
    buildSwapchainForTest(instanceLayers, deviceLayers);

    uint64_t refreshDuration = 0;
    // Verify that Swappy successfully initializes and fetches the refresh cycle duration
    // using the provided mock JNI environment and Vulkan objects.
    bool success = SwappyVk_initAndGetRefreshCycleDuration(gMockEnv, fakeActivity, mPhysicalDev,
                                                           mDevice, mSwapchain, &refreshDuration);

    EXPECT_TRUE(success);

    cleanUpSwapchainForTest();
}

TEST_F(AImageReaderVulkanSwapchainTest, RenderingLoop) {
    // Set up the mocked JNI environment with an SDK version of 23.
    // Different SDK versions trigger different initialization paths in Swappy.
    setupMockJni(23);
    setupMockChoreographer();

    jobject fakeActivity = reinterpret_cast<jobject>(0x1234);

    // Build swapchain to provide a valid device and swapchain to Swappy
    std::vector<const char*> instanceLayers;
    std::vector<const char*> deviceLayers;
    buildSwapchainForTest(instanceLayers, deviceLayers);

    uint64_t refreshDuration = 0;
    bool success = SwappyVk_initAndGetRefreshCycleDuration(gMockEnv, fakeActivity, mPhysicalDev,
                                                           mDevice, mSwapchain, &refreshDuration);

    EXPECT_TRUE(success);

    SwappyVk_setQueueFamilyIndex(mDevice, mPresentQueue, mPresentQueueFamily);
    SwappyVk_setSwapIntervalNS(mDevice, mSwapchain, 16666666);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore imageAvailableSemaphore;
    VK_CHECK(vkCreateSemaphore(mDevice, &semaphoreInfo, nullptr, &imageAvailableSemaphore));

    // Spin for 10 frames, acquiring and presenting to verify the rendering loop
    // correctly interacts with our mocked choreographer.
    for (int i = 0; i < 10; ++i) {
        uint32_t imageIndex;
        VkResult res = vkAcquireNextImageKHR(mDevice, mSwapchain, UINT64_MAX,
                                             imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
        if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
            break;
        }
        EXPECT_EQ(res, VK_SUCCESS);

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &imageAvailableSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &mSwapchain;
        presentInfo.pImageIndices = &imageIndex;

        // Use Swappy to present
        res = SwappyVk_queuePresent(mPresentQueue, &presentInfo);
        EXPECT_EQ(res, VK_SUCCESS);
    }

    vkDestroySemaphore(mDevice, imageAvailableSemaphore, nullptr);

    // Clean up existing mock threads before destroying Swappy to prevent them from
    // calling into a destroyed Swappy instance. TearDown() will clean up any
    // additional threads spawned during destruction.
    cleanupMockChoreographer();

    cleanUpSwapchainForTest();
}

// This test verifies that the VK_LAYER_swappy_mock layer correctly emulates the
// VK_GOOGLE_display_timing extension when enabled. It checks if the extension is
// exposed, sets up a swapchain, queues a frame with mock display timing data,
// and confirms that past presentation timing history can be retrieved successfully.
TEST_F(AImageReaderVulkanSwapchainTest, DisplayTimingMockEnabledTest) {
    // Enables the mock timing behavior in the VK_LAYER_swappy_mock layer.
    // The layer reads this environment variable during vkCreateInstance and, when set to "1",
    // actively intercepts presentation calls to record and return mock timing data.
    setenv("MOCK_VK_GOOGLE_DISPLAY_TIMING", "1", 1);

    std::vector<const char*> instanceLayers = {"VK_LAYER_swappy_mock"};
    std::vector<const char*> deviceLayers = {"VK_LAYER_swappy_mock"};
    std::vector<const char*> deviceExtensions = {
            VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME,
    };

    createVulkanInstance(instanceLayers);
    createAImageReader(640, 480, AIMAGE_FORMAT_PRIVATE, 3);
    getANativeWindowFromReader();
    createVulkanSurface();
    pickPhysicalDeviceAndQueueFamily();

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(mPhysicalDev, "VK_LAYER_swappy_mock", &extensionCount,
                                         nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(mPhysicalDev, "VK_LAYER_swappy_mock", &extensionCount,
                                         availableExtensions.data());

    bool timingExtSupported = false;
    for (const auto& extension : availableExtensions) {
        if (strcmp(extension.extensionName, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME) == 0) {
            timingExtSupported = true;
            break;
        }
    }

    ASSERT_TRUE(timingExtSupported);

    createDeviceAndGetQueue(deviceLayers, deviceExtensions);
    createSwapchain();

    uint32_t imageIndex;
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore imageAcquiredSemaphore = VK_NULL_HANDLE;
    VK_CHECK(vkCreateSemaphore(mDevice, &semInfo, nullptr, &imageAcquiredSemaphore));

    VkResult res = vkAcquireNextImageKHR(mDevice, mSwapchain, UINT64_MAX, imageAcquiredSemaphore,
                                         VK_NULL_HANDLE, &imageIndex);
    VK_CHECK(res);

    VkPresentTimeGOOGLE presentTime = {1, 1000};
    VkPresentTimesInfoGOOGLE presentTimesInfo = {};
    presentTimesInfo.sType = VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE;
    presentTimesInfo.swapchainCount = 1;
    presentTimesInfo.pTimes = &presentTime;

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = &presentTimesInfo;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &mSwapchain;
    presentInfo.pImageIndices = &imageIndex;

    res = vkQueuePresentKHR(mPresentQueue, &presentInfo);
    VK_CHECK(res);

    auto pfnGetPastPresentationTimingGOOGLE = (PFN_vkGetPastPresentationTimingGOOGLE)
            vkGetDeviceProcAddr(mDevice, "vkGetPastPresentationTimingGOOGLE");
    ASSERT_NE(pfnGetPastPresentationTimingGOOGLE, nullptr);

    uint32_t timingCount = 0;
    res = pfnGetPastPresentationTimingGOOGLE(mDevice, mSwapchain, &timingCount, nullptr);
    ASSERT_EQ(res, VK_SUCCESS);
    ASSERT_TRUE(timingCount > 0);

    vkDestroySemaphore(mDevice, imageAcquiredSemaphore, nullptr);
    cleanUpSwapchainForTest();
    unsetenv("MOCK_VK_GOOGLE_DISPLAY_TIMING");
}

// This test verifies that the VK_LAYER_swappy_mock layer behaves transparently
// when its mock functionality is explicitly disabled. It ensures that normal Vulkan
// initialization, enumeration, and swapchain creation still succeed without the
// mock layer interfering.
TEST_F(AImageReaderVulkanSwapchainTest, DisplayTimingMockDisabledTest) {
    // Disables the mock timing behavior in the VK_LAYER_swappy_mock layer.
    // When set to "0", the layer acts as a pass-through and will not emulate the
    // VK_GOOGLE_display_timing extension or intercept its related function calls.
    setenv("MOCK_VK_GOOGLE_DISPLAY_TIMING", "0", 1);

    std::vector<const char*> instanceLayers = {};
    std::vector<const char*> deviceLayers = {"VK_LAYER_swappy_mock"};

    createVulkanInstance(instanceLayers);
    createAImageReader(640, 480, AIMAGE_FORMAT_PRIVATE, 3);
    getANativeWindowFromReader();
    createVulkanSurface();
    pickPhysicalDeviceAndQueueFamily();

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(mPhysicalDev, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(mPhysicalDev, nullptr, &extensionCount,
                                         availableExtensions.data());

    bool timingExtSupported = false;
    for (const auto& extension : availableExtensions) {
        if (strcmp(extension.extensionName, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME) == 0) {
            timingExtSupported = true;
            break;
        }
    }

    std::vector<const char*> deviceExtensions;
    if (timingExtSupported) {
        deviceExtensions.push_back(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
    }

    createDeviceAndGetQueue(deviceLayers, deviceExtensions);
    createSwapchain();
    cleanUpSwapchainForTest();
    unsetenv("MOCK_VK_GOOGLE_DISPLAY_TIMING");
}

class AImageReaderEGLSwapchainTest : public AImageReaderSwapchainTestBase {
public:
    EGLDisplay mDisplay = EGL_NO_DISPLAY;
    EGLSurface mSurface = EGL_NO_SURFACE;
    EGLContext mContext = EGL_NO_CONTEXT;

    void createEGLContext() {
        mDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        ASSERT_NE(mDisplay, EGL_NO_DISPLAY);

        EGLint major, minor;
        ASSERT_TRUE(eglInitialize(mDisplay, &major, &minor));

        const EGLint configAttribs[] = {EGL_RENDERABLE_TYPE,
                                        EGL_OPENGL_ES2_BIT,
                                        EGL_SURFACE_TYPE,
                                        EGL_WINDOW_BIT,
                                        EGL_BLUE_SIZE,
                                        8,
                                        EGL_GREEN_SIZE,
                                        8,
                                        EGL_RED_SIZE,
                                        8,
                                        EGL_NONE};

        EGLConfig config;
        EGLint numConfigs;
        ASSERT_TRUE(eglChooseConfig(mDisplay, configAttribs, &config, 1, &numConfigs));
        ASSERT_GT(numConfigs, 0);

        const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        mContext = eglCreateContext(mDisplay, config, EGL_NO_CONTEXT, contextAttribs);
        ASSERT_NE(mContext, EGL_NO_CONTEXT);

        mSurface = eglCreateWindowSurface(mDisplay, config, mWindow, nullptr);
        ASSERT_NE(mSurface, EGL_NO_SURFACE);

        ASSERT_TRUE(eglMakeCurrent(mDisplay, mSurface, mSurface, mContext));
    }

    void cleanUpEGLForTest() {
        if (mDisplay != EGL_NO_DISPLAY) {
            eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (mSurface != EGL_NO_SURFACE) {
                eglDestroySurface(mDisplay, mSurface);
            }
            if (mContext != EGL_NO_CONTEXT) {
                eglDestroyContext(mDisplay, mContext);
            }
            eglTerminate(mDisplay);
        }
        mDisplay = EGL_NO_DISPLAY;
        mSurface = EGL_NO_SURFACE;
        mContext = EGL_NO_CONTEXT;
    }

    void TearDown() override {
        cleanUpEGLForTest();
        AImageReaderSwapchainTestBase::TearDown();
    }

    void buildEGLForTest() {
        AImageReader_ImageListener listener{};
        listener.context = this;
        listener.onImageAvailable = &AImageReaderSwapchainTestBase::onImageAvailable;
        createAImageReader(640, 480, AIMAGE_FORMAT_RGBA_8888, 3, &listener);
        getANativeWindowFromReader();
        createEGLContext();
    }
};

TEST_F(AImageReaderEGLSwapchainTest, TestHelperMethods) {
    buildEGLForTest();

    ASSERT_NE(mDisplay, EGL_NO_DISPLAY);
    ASSERT_NE(mSurface, EGL_NO_SURFACE);
    ASSERT_NE(mContext, EGL_NO_CONTEXT);
}

TEST_F(AImageReaderEGLSwapchainTest, Initialization) {
    setupMockJni(37);

    jobject fakeActivity = reinterpret_cast<jobject>(0x1234);

    buildEGLForTest();

    bool success = SwappyGL_init(gMockEnv, fakeActivity);
    EXPECT_TRUE(success);

    SwappyGL_setWindow(mWindow);

    SwappyGL_destroy();
}

TEST_F(AImageReaderEGLSwapchainTest, RenderingLoop) {
    // Set up the mocked JNI environment with an SDK version of 23.
    setupMockJni(23);
    setupMockChoreographer();

    jobject fakeActivity = reinterpret_cast<jobject>(0x1234);

    buildEGLForTest();

    bool success = SwappyGL_init(gMockEnv, fakeActivity);
    EXPECT_TRUE(success);

    SwappyGL_setWindow(mWindow);
    SwappyGL_setSwapIntervalNS(16666666);

    for (int i = 0; i < 10; ++i) {
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        EXPECT_TRUE(SwappyGL_swap(mDisplay, mSurface));
    }

    // Clean up existing mock threads before destroying Swappy to prevent them from
    // calling into a destroyed Swappy instance. TearDown() will clean up any
    // additional threads spawned during destruction.
    cleanupMockChoreographer();

    SwappyGL_destroy();
}

} // namespace swappytest
} // namespace android
