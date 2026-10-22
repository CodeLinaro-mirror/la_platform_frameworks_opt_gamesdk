#include <jni.h>
#include <stdlib.h>
#include <unistd.h>

#include "game-activity/native_app_glue/android_native_app_glue.h"

#ifdef AGDK_COVERAGE
#include "gcov_flush.h"
#endif

namespace {
int on_pause_sleep_duration_ms = 0;
} // namespace

void handle_cmd(struct android_app* app, int32_t cmd) {
    if (cmd == APP_CMD_PAUSE) {
        if (on_pause_sleep_duration_ms > 0) {
            usleep(on_pause_sleep_duration_ms * 1000);
        }
    }
#ifdef AGDK_COVERAGE
    if (cmd == APP_CMD_PAUSE || cmd == APP_CMD_STOP || cmd == APP_CMD_DESTROY) {
        agdk::testing::FlushGcovCounters();
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
                agdk::testing::FlushGcovCounters();
#endif
                return;
            }
        }
    }
}
}