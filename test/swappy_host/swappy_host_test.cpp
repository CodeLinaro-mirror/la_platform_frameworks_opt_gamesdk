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

#include <gtest/gtest.h>
#include "Settings.h"

using namespace swappy;
using namespace std::chrono_literals;

class SwappyHostTest : public ::testing::Test {
protected:
    void SetUp() override {
        Settings::reset();
    }

    void TearDown() override {
        Settings::reset();
    }
};

TEST_F(SwappyHostTest, DefaultSettings) {
    Settings* settings = Settings::getInstance();
    ASSERT_NE(settings, nullptr);

    EXPECT_EQ(settings->getSwapDuration(), 16666667ns);
    EXPECT_TRUE(settings->getUseAffinity());

    auto timings = settings->getDisplayTimings();
    EXPECT_EQ(timings.refreshPeriod, 0ns);
    EXPECT_EQ(timings.appOffset, 0ns);
    EXPECT_EQ(timings.sfOffset, 0ns);
}

TEST_F(SwappyHostTest, SetDisplayTimings) {
    Settings* settings = Settings::getInstance();
    ASSERT_NE(settings, nullptr);

    Settings::DisplayTimings newTimings{16666666ns, 2000000ns, 1000000ns};
    settings->setDisplayTimings(newTimings);

    auto updatedTimings = settings->getDisplayTimings();
    EXPECT_EQ(updatedTimings.refreshPeriod, 16666666ns);
    EXPECT_EQ(updatedTimings.appOffset, 2000000ns);
    EXPECT_EQ(updatedTimings.sfOffset, 1000000ns);
}

TEST_F(SwappyHostTest, SetSwapDurationAndAffinity) {
    Settings* settings = Settings::getInstance();
    ASSERT_NE(settings, nullptr);

    settings->setSwapDuration(33333333ULL);
    EXPECT_EQ(settings->getSwapDuration(), 33333333ns);

    settings->setUseAffinity(false);
    EXPECT_FALSE(settings->getUseAffinity());
}

TEST_F(SwappyHostTest, ListenerCallbacks) {
    Settings* settings = Settings::getInstance();
    ASSERT_NE(settings, nullptr);

    int callCount = 0;
    settings->addListener([&callCount]() {
        callCount++;
    });

    settings->setSwapDuration(20000000ULL);
    EXPECT_EQ(callCount, 1);

    settings->setUseAffinity(true);
    EXPECT_EQ(callCount, 2);

    settings->removeAllListeners();
    settings->setSwapDuration(10000000ULL);
    EXPECT_EQ(callCount, 2);
}
