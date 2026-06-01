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
package com.samples.cube;

import android.view.View;
import androidx.test.espresso.Espresso;
import androidx.test.espresso.UiController;
import androidx.test.espresso.ViewAction;
import androidx.test.espresso.action.ViewActions;
import androidx.test.espresso.assertion.ViewAssertions;
import androidx.test.espresso.matcher.ViewMatchers;
import androidx.test.ext.junit.rules.ActivityScenarioRule;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import org.hamcrest.Matcher;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class CubeActivityTest {
    @Rule
    public ActivityScenarioRule<CubeActivity> activityRule =
            new ActivityScenarioRule<>(CubeActivity.class);

    @Before
    public void setUp() {
        // The settings panel is hidden by default. Click the settings icon in the action bar to
        // reveal it
        Espresso.onView(ViewMatchers.withId(R.id.settings_item)).perform(ViewActions.click());
    }

    @Test
    public void testTogglePresentTiming() {
        // Toggle the VK_EXT_present_timing switch and verify it doesn't crash the app
        // Tap to open the settings panel if it's hidden, though looking at the layout
        // it might be visible or requires tapping the screen.
        // Assuming the switches are accessible in the UI tree
        Espresso.onView(ViewMatchers.withId(R.id.switchPresentTiming)).perform(ViewActions.click());

        // Assert it is now checked
        Espresso.onView(ViewMatchers.withId(R.id.switchPresentTiming))
                .check(ViewAssertions.matches(ViewMatchers.isChecked()));

        // Toggle back
        Espresso.onView(ViewMatchers.withId(R.id.switchPresentTiming)).perform(ViewActions.click());

        // Assert it is now unchecked
        Espresso.onView(ViewMatchers.withId(R.id.switchPresentTiming))
                .check(ViewAssertions.matches(ViewMatchers.isNotChecked()));
    }

    @Test
    public void testToggle30FpsLimit() {
        // Toggle the 30 fps limit switch and verify no crash occurs
        // (this tests the race condition bug we fixed)
        Espresso.onView(ViewMatchers.withId(R.id.switch30FpsLimit)).perform(ViewActions.click());

        Espresso.onView(ViewMatchers.withId(R.id.switch30FpsLimit))
                .check(ViewAssertions.matches(ViewMatchers.isChecked()));

        Espresso.onView(ViewMatchers.withId(R.id.switch30FpsLimit)).perform(ViewActions.click());

        Espresso.onView(ViewMatchers.withId(R.id.switch30FpsLimit))
                .check(ViewAssertions.matches(ViewMatchers.isNotChecked()));
    }

    @Test
    public void testToggleDisplayTimingAndPresentTiming() {
        // Turn on Display Timing
        Espresso.onView(ViewMatchers.withId(R.id.switchGoogleTiming)).perform(ViewActions.click());

        // Now turn on Present Timing (which should uncheck Display Timing)
        Espresso.onView(ViewMatchers.withId(R.id.switchPresentTiming)).perform(ViewActions.click());

        // Verify mutual exclusion
        Espresso.onView(ViewMatchers.withId(R.id.switchPresentTiming))
                .check(ViewAssertions.matches(ViewMatchers.isChecked()));
        Espresso.onView(ViewMatchers.withId(R.id.switchGoogleTiming))
                .check(ViewAssertions.matches(ViewMatchers.isNotChecked()));
    }

    @Test
    public void testFixed30FpsActual() throws InterruptedException {
        // Toggle the 30 fps limit switch
        Espresso.onView(ViewMatchers.withId(R.id.switch30FpsLimit)).perform(ViewActions.click());

        // Wait a few seconds for the frame rate to stabilize and the UI to update.
        // The UI updates its FPS counter every 1 second
        Espresso.onView(ViewMatchers.isRoot()).perform(waitFor(3000));

        // Verify the Target FPS is around 30
        Espresso.onView(ViewMatchers.withId(R.id.target_fps_text))
                .check(ViewAssertions.matches(ViewMatchers.withText(
                        org.hamcrest.Matchers.containsString("Target FPS: 30."))));

        // Parse the Actual FPS to ensure it's successfully hovering around 30
        Espresso.onView(ViewMatchers.withId(R.id.target_fps_text))
                .check((view, noViewFoundException) -> {
                    if (noViewFoundException != null)
                        throw noViewFoundException;
                    String text = ((android.widget.TextView) view).getText().toString();
                    String[] parts = text.split("Actual FPS: ");
                    org.junit.Assert.assertTrue(
                            "Text didn't contain Actual FPS: " + text, parts.length > 1);

                    try {
                        double actualFps = Double.parseDouble(parts[1].trim());
                        // Allow some variance depending on emulator/device performance, but it
                        // should be near 30.
                        org.junit.Assert.assertTrue(
                                "Expected actual FPS around 30, but was " + actualFps,
                                actualFps >= 20.0 && actualFps <= 40.0);
                    } catch (NumberFormatException e) {
                        org.junit.Assert.fail("Failed to parse Actual FPS: " + parts[1]);
                    }
                });
    }

    @Test
    public void testFixed30FpsWithPresentTimingActual() throws InterruptedException {
        // Toggle the present timing switch first
        Espresso.onView(ViewMatchers.withId(R.id.switchPresentTiming)).perform(ViewActions.click());

        // Toggle the 30 fps limit switch
        Espresso.onView(ViewMatchers.withId(R.id.switch30FpsLimit)).perform(ViewActions.click());

        // Wait a few seconds for the frame rate to stabilize and the UI to update.
        Espresso.onView(ViewMatchers.isRoot()).perform(waitFor(3000));

        // Verify the Target FPS is around 30
        Espresso.onView(ViewMatchers.withId(R.id.target_fps_text))
                .check(ViewAssertions.matches(ViewMatchers.withText(
                        org.hamcrest.Matchers.containsString("Target FPS: 30."))));

        // Parse the Actual FPS to ensure it's successfully hovering around 30
        Espresso.onView(ViewMatchers.withId(R.id.target_fps_text))
                .check((view, noViewFoundException) -> {
                    if (noViewFoundException != null)
                        throw noViewFoundException;
                    String text = ((android.widget.TextView) view).getText().toString();
                    String[] parts = text.split("Actual FPS: ");
                    org.junit.Assert.assertTrue(
                            "Text didn't contain Actual FPS: " + text, parts.length > 1);

                    try {
                        double actualFps = Double.parseDouble(parts[1].trim());
                        org.junit.Assert.assertTrue(
                                "Expected actual FPS around 30 with present timing, but was "
                                        + actualFps,
                                actualFps >= 20.0 && actualFps <= 40.0);
                    } catch (NumberFormatException e) {
                        org.junit.Assert.fail("Failed to parse Actual FPS: " + parts[1]);
                    }
                });
    }

    public static ViewAction waitFor(long delay) {
        return new ViewAction() {
            @Override
            public Matcher<View> getConstraints() {
                return ViewMatchers.isRoot();
            }

            @Override
            public String getDescription() {
                return "wait for " + delay + " milliseconds";
            }

            @Override
            public void perform(UiController uiController, View view) {
                uiController.loopMainThreadForAtLeast(delay);
            }
        };
    }
}
