// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package com.google.android.games.paddleboat;

import static com.google.common.truth.Truth.assertThat;

import android.content.Context;
import android.view.InputDevice;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.filters.LargeTest;
import org.junit.Test;

@LargeTest
public class GameControllerManagerTest {
  public Context context = ApplicationProvider.getApplicationContext();

  public GameControllerManager gameControllerManager = new GameControllerManager(context, false);

  @Test
  public void generateSourceString_allSourceClasses_producesCorrectString() {
    String sourceString = gameControllerManager.generateSourceString(InputDevice.SOURCE_CLASS_BUTTON
        | InputDevice.SOURCE_CLASS_JOYSTICK | InputDevice.SOURCE_CLASS_POINTER
        | InputDevice.SOURCE_CLASS_POSITION | InputDevice.SOURCE_CLASS_TRACKBALL);
    assertThat(sourceString).contains("Source Classes: BUTTON JOYSTICK POINTER POSITION TRACKBALL");
  }

  @Test
  public void generateSourceString_allSources_producesCorrectString() {
    String sourceString = gameControllerManager.generateSourceString(
        InputDevice.SOURCE_BLUETOOTH_STYLUS | InputDevice.SOURCE_DPAD | InputDevice.SOURCE_HDMI
        | InputDevice.SOURCE_JOYSTICK | InputDevice.SOURCE_KEYBOARD | InputDevice.SOURCE_MOUSE
        | InputDevice.SOURCE_MOUSE_RELATIVE | InputDevice.SOURCE_ROTARY_ENCODER
        | InputDevice.SOURCE_STYLUS | InputDevice.SOURCE_TOUCHPAD | InputDevice.SOURCE_TOUCHSCREEN
        | InputDevice.SOURCE_TOUCH_NAVIGATION | InputDevice.SOURCE_TRACKBALL);
    assertThat(sourceString)
        .contains("Sources: BLUETOOTH_STYLUS DPAD HDMI JOYSTICK KEYBOARD MOUSE MOUSE_RELATIVE "
            + "ROTARY_ENCODER STYLUS TOUCHPAD TOUCHSCREEN TOUCH_NAVIGATION TRACKBALL");
  }
}
