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

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <thread>

namespace swappy {

// Estimates display vsync times from Choreographer callback timestamps and
// sleeps a ChoreographerFilter worker thread until the next vsync.
class ChoreographerTimer {
public:
    using time_point = std::chrono::steady_clock::time_point;

    ChoreographerTimer(std::chrono::nanoseconds refreshPeriod,
                       std::chrono::nanoseconds appToSfDelay)
          : mRefreshPeriod(refreshPeriod), mAppToSfDelay(appToSfDelay) {}

    // Returns false if we have detected that we have received the same
    // timestamp multiple times so that the caller can wait for fresh timestamps
    bool addTimestamp(time_point point) {
        // Keep track of the previous timestamp and how many times we've seen it
        // to determine if we've stopped receiving Choreographer callbacks,
        // which would indicate that we should probably stop until we see them
        // again (e.g., if the app has been moved to the background)
        if (point == mLastTimestamp) {
            if (mRepeatCount++ > 5) {
                return false;
            }
        } else {
            mRepeatCount = 0;
        }
        mLastTimestamp = point;

        point += mAppToSfDelay;

        bool moreThanOneRefreshPeriodElapsed = mBaseTime + mRefreshPeriod * 1.5 < point;
        if (moreThanOneRefreshPeriodElapsed) {
            do {
                mBaseTime += mRefreshPeriod;
            } while (mBaseTime + mRefreshPeriod * 1.5 < point);
            mBaseTime += mRefreshPeriod;
            // Long waits pollute the filter so don't adjust refreshPeriod.
            return true;
        }

        std::chrono::nanoseconds delta = (point - (mBaseTime + mRefreshPeriod));
        if (delta < -mRefreshPeriod / 2) {
            // Also ignore short intervals
            return true;
        }

        // Exponential smoothing factor = 0.04 avoids roughness.
        mRefreshPeriod += delta / 25;
        mBaseTime += mRefreshPeriod;

        return true;
    }

    void sleep(std::chrono::nanoseconds offset) {
        if (offset < -(mRefreshPeriod / 2) || offset > mRefreshPeriod / 2) {
            offset = std::chrono::nanoseconds::zero();
        }

        const auto now = std::chrono::steady_clock::now();
        auto targetTime = mBaseTime + mRefreshPeriod + offset;
        while (targetTime < now) {
            targetTime += mRefreshPeriod;
        }

        std::this_thread::sleep_until(targetTime);
    }

private:
    std::chrono::nanoseconds mRefreshPeriod;
    const std::chrono::nanoseconds mAppToSfDelay;
    time_point mBaseTime = std::chrono::steady_clock::now();

    time_point mLastTimestamp = std::chrono::steady_clock::now();
    std::optional<std::chrono::nanoseconds> mSfToVsyncDelay;
    int32_t mRepeatCount = 0;
};

} // namespace swappy
