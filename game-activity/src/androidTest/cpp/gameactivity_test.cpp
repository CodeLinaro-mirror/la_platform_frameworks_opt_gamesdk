#include <jni.h>
#include <stdlib.h>
#include <unistd.h>

#include "game-activity/native_app_glue/android_native_app_glue.h"

#ifdef AGDK_COVERAGE
extern "C" void __gcov_dump(void) __attribute__((weak));
extern "C" void __gcov_reset(void) __attribute__((weak));
extern "C" void __gcov_flush(void) __attribute__((weak));
#endif

namespace {
int on_pause_sleep_duration_ms = 0;

#ifdef AGDK_COVERAGE
void FlushGcovCounters() {
    setenv("GCOV_PREFIX",
           "/data/data/com.google.androidgamesdk.gameactivity.test/files",
           /*overwrite=*/0);
    setenv("GCOV_PREFIX_STRIP", "20", /*overwrite=*/0);
    if (__gcov_dump) {
        __gcov_dump();
        if (__gcov_reset) {
            __gcov_reset();
        }
    } else if (__gcov_flush) {
        __gcov_flush();
    }
}
#endif
}  // namespace

void handle_cmd(struct android_app* app, int32_t cmd) {
    if (cmd == APP_CMD_PAUSE) {
        if (on_pause_sleep_duration_ms > 0) {
            usleep(on_pause_sleep_duration_ms * 1000);
        }
    }
#ifdef AGDK_COVERAGE
    if (cmd == APP_CMD_PAUSE || cmd == APP_CMD_STOP || cmd == APP_CMD_DESTROY) {
        FlushGcovCounters();
    }
#endif
}

extern "C" {

JNIEXPORT void JNICALL Java_com_google_androidgamesdk_TestGameActivity_setOnPauseSleepDurationMs(
        JNIEnv* env, jclass clazz, jint durationMs) {
    on_pause_sleep_duration_ms = durationMs;
}

void android_main(struct android_app* app) {
    app->onAppCmd = handle_cmd;

    while (true) {
        int events;
        struct android_poll_source* source;

        while (ALooper_pollOnce(0, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) {
#ifdef AGDK_COVERAGE
                FlushGcovCounters();
#endif
                return;
            }
        }
    }
}
}