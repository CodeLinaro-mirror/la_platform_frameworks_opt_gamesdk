/*
 * Copyright (C) 2026 The Android Open Source Project
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

package com.google.androidgamesdk;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.mockito.Mockito.mock;

import android.graphics.PixelFormat;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import androidx.core.graphics.Insets;
import androidx.core.view.WindowInsetsCompat;
import com.google.androidgamesdk.gametextinput.State;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;

@RunWith(RobolectricTestRunner.class)
@Config(sdk = 31)
public class GameActivityUnitTest {
    private GameActivity activity;

    @Before
    public void setUp() {
        activity = Robolectric.buildActivity(GameActivity.class).get();
    }

    @Test
    public void getImeEditorInfo_defaultValuesAreSetCorrectly() {
        EditorInfo editorInfo = activity.getImeEditorInfo();

        assertNotNull("EditorInfo should not be null", editorInfo);
        assertEquals("Default inputType should be TYPE_CLASS_TEXT", InputType.TYPE_CLASS_TEXT,
                editorInfo.inputType);
        assertEquals("Default actionId should be IME_ACTION_DONE", EditorInfo.IME_ACTION_DONE,
                editorInfo.actionId);
        assertEquals("Default imeOptions should match IME_ACTION_DONE | IME_FLAG_NO_FULLSCREEN",
                EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_FULLSCREEN,
                editorInfo.imeOptions);
    }

    @Test
    public void getImeEditorInfo_returnsCachedInstance() {
        EditorInfo first = activity.getImeEditorInfo();
        EditorInfo second = activity.getImeEditorInfo();

        assertSame("Subsequent calls should return the same EditorInfo instance", first, second);
    }

    @Test
    public void setImeEditorInfo_updatesEditorInfo() {
        EditorInfo customInfo = new EditorInfo();
        customInfo.inputType = InputType.TYPE_CLASS_NUMBER;
        customInfo.actionId = EditorInfo.IME_ACTION_GO;

        activity.setImeEditorInfo(customInfo);

        EditorInfo retrieved = activity.getImeEditorInfo();
        assertEquals(InputType.TYPE_CLASS_NUMBER, retrieved.inputType);
        assertEquals(EditorInfo.IME_ACTION_GO, retrieved.actionId);
    }

    @Test
    public void setImeEditorInfoFields_whenSurfaceViewIsNull_doesNotThrow() {
        activity.mSurfaceView = null;
        activity.setImeEditorInfoFields(InputType.TYPE_CLASS_PHONE, EditorInfo.IME_ACTION_SEND,
                EditorInfo.IME_FLAG_NO_EXTRACT_UI);
    }

    @Test
    public void setTextInputState_whenSurfaceViewIsNull_doesNotThrow() {
        activity.mSurfaceView = null;
        activity.setTextInputState(new State("12345", 0, 5, -1, -1));
    }

    @Test
    public void getWindowInsets_whenSurfaceViewIsNull_returnsNull() {
        activity.mSurfaceView = null;
        Insets insets = activity.getWindowInsets(WindowInsetsCompat.Type.ime());
        assertNull("Expected null when surface view is not attached or root window insets are null",
                insets);
    }

    @Test
    public void getWaterfallInsets_whenSurfaceViewIsNull_returnsNull() {
        activity.mSurfaceView = null;
        Insets insets = activity.getWaterfallInsets();
        assertNull("Expected null waterfall insets when surface view is null", insets);
    }

    @Test
    public void getGameActivityNativeHandle_whenNativeDestroyed_returnsZero() {
        assertEquals("Expected 0 when native handle is not initialized", 0L,
                activity.getGameActivityNativeHandle());
    }

    @Test
    public void processMotionEvent_whenNativeDestroyed_returnsFalse() {
        MotionEvent event = MotionEvent.obtain(0, 0, MotionEvent.ACTION_DOWN, 0f, 0f, 0);
        assertFalse(activity.processMotionEvent(event));
        event.recycle();
    }

    @Test
    public void onKeyUp_whenNativeDestroyed_returnsFalse() {
        KeyEvent event = new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_A);
        assertFalse(activity.onKeyUp(KeyEvent.KEYCODE_A, event));
    }

    @Test
    public void onKeyDown_whenNativeDestroyed_returnsFalse() {
        KeyEvent event = new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_A);
        assertFalse(activity.onKeyDown(KeyEvent.KEYCODE_A, event));
    }

    @Test
    public void stateChanged_whenNativeDestroyed_doesNotThrow() {
        activity.stateChanged(new State("test", 0, 4, -1, -1), false);
    }

    @Test
    public void onGlobalLayout_whenNativeDestroyed_doesNotThrow() {
        activity.onGlobalLayout();
    }

    @Test
    public void onImeInsetsChanged_doesNotThrow() {
        activity.onImeInsetsChanged(Insets.NONE);
    }

    @Test
    public void onTrimMemory_whenNativeDestroyed_doesNotThrow() {
        activity.onTrimMemory(10);
    }

    @Test
    public void onWindowFocusChanged_whenNativeDestroyed_doesNotThrow() {
        activity.onWindowFocusChanged(true);
        activity.onWindowFocusChanged(false);
    }

    @Test
    public void surfaceCreated_whenNativeDestroyed_doesNotThrow() {
        SurfaceHolder holder = mock(SurfaceHolder.class);
        activity.surfaceCreated(holder);
    }

    @Test
    public void surfaceChanged_whenNativeDestroyed_doesNotThrow() {
        SurfaceHolder holder = mock(SurfaceHolder.class);
        activity.surfaceChanged(holder, PixelFormat.RGB_565, 1080, 1920);
    }

    @Test
    public void surfaceRedrawNeeded_whenNativeDestroyed_doesNotThrow() {
        SurfaceHolder holder = mock(SurfaceHolder.class);
        activity.surfaceRedrawNeeded(holder);
    }

    @Test
    public void surfaceDestroyed_whenNativeDestroyed_doesNotThrow() {
        SurfaceHolder holder = mock(SurfaceHolder.class);
        activity.surfaceDestroyed(holder);
    }

    @Test
    public void onEditorAction_whenNativeDestroyed_doesNotThrow() {
        activity.onEditorAction(EditorInfo.IME_ACTION_DONE);
    }

    @Test
    public void onSoftwareKeyboardVisibilityChanged_whenNativeDestroyed_doesNotThrow() {
        activity.onSoftwareKeyboardVisibilityChanged(true);
        activity.onSoftwareKeyboardVisibilityChanged(false);
    }

    @Test
    public void onApplyWindowInsets_whenNativeDestroyed_returnsPassedInsets() {
        View view = mock(View.class);
        WindowInsetsCompat insets = new WindowInsetsCompat.Builder().build();
        WindowInsetsCompat result = activity.onApplyWindowInsets(view, insets);
        assertSame(insets, result);
    }

    @Test
    public void setWindowFlags_setsFlagsOnWindow() {
        activity.setWindowFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON,
                WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        assertEquals(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON,
                activity.getWindow().getAttributes().flags
                        & WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    @Test
    public void setWindowFormat_setsFormatOnWindow() {
        activity.setWindowFormat(PixelFormat.RGB_565);
        assertEquals(PixelFormat.RGB_565, activity.getWindow().getAttributes().format);
    }

    @Test
    public void createSurfaceView_returnsInputEnabledSurfaceView() {
        GameActivity.InputEnabledSurfaceView surfaceView = activity.createSurfaceView();
        assertNotNull(surfaceView);
        assertNotNull(surfaceView.getEditorInfo());
    }
}
