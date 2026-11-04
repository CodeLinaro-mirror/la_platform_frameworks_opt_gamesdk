/*
 * Copyright 2018 The Android Open Source Project
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

#include "ChoreographerFilter.h"

#define LOG_TAG "ChoreographerFilter"

#include <sched.h>
#include <unistd.h>

#include <deque>
#include <string>
#include <thread>

#include "ChoreographerTimer.h"
#include "Settings.h"
#include "SwappyLog.h"
#include "Thread.h"
#include "Trace.h"

namespace swappy {

ChoreographerFilter::ChoreographerFilter(std::chrono::nanoseconds refreshPeriod,
                                         std::chrono::nanoseconds appToSfDelay, Worker doWork)
      : mRefreshPeriod(refreshPeriod), mAppToSfDelay(appToSfDelay), mDoWork(doWork) {
    Settings::getInstance()->addListener([this]() { onSettingsChanged(); });

    std::lock_guard<std::mutex> lock(mThreadPoolMutex);
    mUseAffinity = Settings::getInstance()->getUseAffinity();
    launchThreadsLocked();
}

ChoreographerFilter::~ChoreographerFilter() {
    std::lock_guard<std::mutex> lock(mThreadPoolMutex);
    terminateThreadsLocked();
}

void ChoreographerFilter::onChoreographer(std::optional<std::chrono::nanoseconds> sfToVsyncDelay) {
    std::lock_guard<std::mutex> lock(mMutex);
    mLastTimestamp = std::chrono::steady_clock::now();
    mSfToVsyncDelay = sfToVsyncDelay;
    ++mSequenceNumber;
    mCondition.notify_all();
}

void ChoreographerFilter::launchThreadsLocked() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mIsRunning = true;
    }

    const int32_t numThreads = getNumCpus() > 2 ? 2 : 1;
    for (int32_t thread = 0; thread < numThreads; ++thread) {
        mThreadPool.push_back(Thread([this, thread]() { threadMain(mUseAffinity, thread); }));
    }
}

void ChoreographerFilter::terminateThreadsLocked() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mIsRunning = false;
        mCondition.notify_all();
    }

    for (auto& thread : mThreadPool) {
        thread.join();
    }
    mThreadPool.clear();
}

void ChoreographerFilter::onSettingsChanged() {
    const bool useAffinity = Settings::getInstance()->getUseAffinity();
    const Settings::DisplayTimings displayTimings = Settings::getInstance()->getDisplayTimings();
    std::lock_guard<std::mutex> lock(mThreadPoolMutex);
    if (useAffinity == mUseAffinity && mRefreshPeriod == displayTimings.refreshPeriod) {
        return;
    }

    terminateThreadsLocked();
    mUseAffinity = useAffinity;
    mRefreshPeriod = displayTimings.refreshPeriod;
    mAppToSfDelay = displayTimings.sfOffset - displayTimings.appOffset;
    SWAPPY_LOGV("onSettingsChanged(): refreshPeriod=%lld, appOffset=%lld, "
                "sfOffset=%lld",
                (long long)displayTimings.refreshPeriod.count(),
                (long long)displayTimings.appOffset.count(),
                (long long)displayTimings.sfOffset.count());
    launchThreadsLocked();
}

void ChoreographerFilter::threadMain(bool useAffinity, int32_t thread) {
    ChoreographerTimer timer(mRefreshPeriod, mAppToSfDelay);

    {
        int cpu = getNumCpus() - 1 - thread;
        if (cpu >= 0) {
            setAffinity(cpu);
        }
    }

    std::string threadName = "Filter";
    threadName += swappy::to_string(thread);
    pthread_setname_np(pthread_self(), threadName.c_str());

    std::unique_lock<std::mutex> lock(mMutex);
    while (true) {
        auto timestamp = mLastTimestamp;
        auto workDuration = mWorkDuration;
        lock.unlock();

        // If we have received the same timestamp multiple times, it probably
        // means that the app has stopped sending them to us, which could
        // indicate that it's no longer running. If we detect that, we stop
        // until we see a fresh timestamp to avoid spinning forever in the
        // background.
        if (!timer.addTimestamp(timestamp)) {
            lock.lock();
            mCondition.wait(lock, [=]() { return !mIsRunning || (mLastTimestamp != timestamp); });
            timestamp = mLastTimestamp;
            lock.unlock();
            timer.addTimestamp(timestamp);
        }

        if (!mIsRunning) break;

        timer.sleep(-workDuration);
        {
            std::unique_lock<std::mutex> workLock(mWorkMutex);
            const auto now = std::chrono::steady_clock::now();
            if (now - mLastWorkRun > mRefreshPeriod / 2) {
                // Assume we got here first and there's work to do
                gamesdk::ScopedTrace trace("doWork");
                mWorkDuration = mDoWork(mSfToVsyncDelay);
                mLastWorkRun = now;
            }
        }
        lock.lock();
    }
}

} // namespace swappy
