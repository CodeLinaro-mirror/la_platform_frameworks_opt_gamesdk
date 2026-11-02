/*
 * Copyright 2020 The Android Open Source Project
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

package com.swappy.testapp;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.method.ScrollingMovementMethod;
import android.widget.TextView;
import androidx.appcompat.app.AppCompatActivity;

public class MainActivity extends AppCompatActivity {
    private static final long POLL_INTERVAL_MS = 1000L;
    private static final Handler sMainHandler = new Handler(Looper.getMainLooper());
    private static final Object sLock = new Object();
    // GoogleTest supports only one RUN_ALL_TESTS() call per process.
    private static boolean sStarted = false;
    // Null until the test run ends.
    private static String sResult = null;
    // Accessed on the main thread between onStart() and onStop().
    private static Runnable sActivePollRunnable = null;

    private TextView mTextView;
    private final Runnable mPollRunnable = new Runnable() {
        @Override
        public void run() {
            if (isFinishing() || isDestroyed()) {
                return;
            }
            final String result;
            synchronized (sLock) {
                result = sResult;
            }
            if (result != null) {
                mTextView.setText(result);
            } else {
                mTextView.setText(testSummarySoFar());
                sMainHandler.postDelayed(this, POLL_INTERVAL_MS);
            }
        }
    };

    // Used to load the test app's native library on application startup.
    static {
        System.loadLibrary("swappy-test");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        mTextView = findViewById(R.id.sample_text);
        mTextView.setMovementMethod(new ScrollingMovementMethod());
        startTestsOnce();
    }

    @Override
    protected void onStart() {
        super.onStart();
        sActivePollRunnable = mPollRunnable;
        sMainHandler.post(mPollRunnable);
    }

    @Override
    protected void onStop() {
        if (sActivePollRunnable == mPollRunnable) {
            sActivePollRunnable = null;
        }
        sMainHandler.removeCallbacks(mPollRunnable);
        super.onStop();
    }

    private static void startTestsOnce() {
        synchronized (sLock) {
            if (sStarted) {
                return;
            }
            sStarted = true;
        }
        Thread testThread = new Thread(() -> {
            String result = "";
            try {
                result = runTests();
            } finally {
                synchronized (sLock) {
                    sResult = result;
                }
                // Show the result now instead of at the next poll.
                sMainHandler.post(() -> {
                    if (sActivePollRunnable != null) {
                        sMainHandler.removeCallbacks(sActivePollRunnable);
                        sActivePollRunnable.run();
                    }
                });
            }
        }, "SwappyTestRunner");
        testThread.start();
    }

    /**
     * Native methods implemented in app's native library,
     * which is packaged with this application.
     */
    public static native String runTests();
    public static native String testSummarySoFar();
}
