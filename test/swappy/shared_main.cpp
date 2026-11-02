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

#include <algorithm>
#include <iterator>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "swappy_test.h"

#define LOG_TAG "SwappyTestMain"
#include "../../samples/common/include/Log.h"

using ::testing::EmptyTestEventListener;
using ::testing::TestEventListener;
using ::testing::TestEventListeners;
using ::testing::TestInfo;
using ::testing::TestPartResult;
using ::testing::UnitTest;

namespace {

class ScopedTestListener {
public:
    ScopedTestListener(TestEventListeners& listeners, TestEventListener* listener)
          : listeners_(listeners), listener_(listener) {
        listeners_.Append(listener_);
    }

    ~ScopedTestListener() {
        listeners_.Release(listener_);
    }

    ScopedTestListener(const ScopedTestListener&) = delete;
    ScopedTestListener& operator=(const ScopedTestListener&) = delete;

private:
    TestEventListeners& listeners_;
    TestEventListener* listener_;
};

// Record the output of googletest tests
class GTestRecorder : public EmptyTestEventListener {
    mutable std::mutex mutex;
    std::set<std::string> tests_started;
    std::set<std::string> tests_completed;
    std::set<std::string> tests_failed;
    std::vector<std::string> success_invocations; // Only from SUCCESS macros
    std::vector<std::string> failed_invocations;  // From any failed EXPECT or ASSERT
    bool overall_success = false;
    std::string current_test;

private:
    // Called before any test activity starts.
    void OnTestProgramStart(const UnitTest& /* unit_test */) override {
        std::lock_guard<std::mutex> lock(mutex);
        tests_started.clear();
        tests_completed.clear();
        tests_failed.clear();
        success_invocations.clear();
        failed_invocations.clear();
        overall_success = false;
        current_test.clear();
    }

    // Called after all test activities have ended.
    void OnTestProgramEnd(const UnitTest& unit_test) override {
        const bool passed = unit_test.Passed();
        std::lock_guard<std::mutex> lock(mutex);
        overall_success = passed;
        current_test.clear();
    }

    // Called before a test starts.
    void OnTestStart(const TestInfo& test_info) override {
        std::string test_name = std::string(test_info.test_case_name()) + "." + test_info.name();
        {
            std::lock_guard<std::mutex> lock(mutex);
            current_test = test_name;
            tests_started.insert(current_test);
        }
        ALOGI("TestStarted: %s", test_name.c_str());
    }

    // Called after a failed assertion or a SUCCEED() invocation.
    void OnTestPartResult(const TestPartResult& test_part_result) override {
        const char* file_name =
                test_part_result.file_name() != nullptr ? test_part_result.file_name() : "";
        std::lock_guard<std::mutex> lock(mutex);
        std::stringstream record;
        record << current_test << " " << file_name << ":" << test_part_result.line_number() << '\n'
               << test_part_result.summary() << '\n';
        if (test_part_result.failed()) {
            failed_invocations.push_back(record.str());
            tests_failed.insert(current_test);
            ALOGI("TestFailed: %s\n%s", current_test.c_str(), record.str().c_str());
        } else {
            success_invocations.push_back(record.str());
        }
    }

    // Called after a test ends.
    void OnTestEnd(const TestInfo& test_info) override {
        std::string test_name = std::string(test_info.test_case_name()) + "." + test_info.name();
        std::lock_guard<std::mutex> lock(mutex);
        tests_completed.insert(std::move(test_name));
        current_test.clear();
    }

public:
    std::string GetResult() const {
        std::lock_guard<std::mutex> lock(mutex);
        std::stringstream result;
        result << "TESTS " << (overall_success ? "SUCCEEDED" : "FAILED") << '\n';
        result << "\nTests that ran to completion:\n";
        for (const auto& test_name : tests_completed) {
            result << test_name << '\n';
        }
        std::set<std::string> not_completed;
        std::set_difference(tests_started.begin(), tests_started.end(), tests_completed.begin(),
                            tests_completed.end(),
                            std::inserter(not_completed, not_completed.end()));
        if (!not_completed.empty()) {
            result << "\nTests that started but failed to complete:\n";
            for (const auto& test_name : not_completed) {
                result << test_name << '\n';
            }
        }
        if (!tests_failed.empty()) {
            result << "\nFailed tests:\n";
            for (const auto& test_name : tests_failed) {
                result << test_name << '\n';
            }
        }
        if (!success_invocations.empty()) {
            result << "\nExplicitly recorded successes:\n";
            for (const auto& invocation : success_invocations) {
                result << invocation << '\n';
            }
        }
        if (!failed_invocations.empty()) {
            result << "\nFailure details:\n";
            for (const auto& invocation : failed_invocations) {
                result << invocation << '\n';
            }
        }
        return result.str();
    }

    std::string Summary() const {
        std::lock_guard<std::mutex> lock(mutex);
        std::stringstream summary_stream;
        summary_stream << "Running:\n" << current_test << '\n';
        summary_stream << "\nCompleted:\n";
        for (const auto& test_name : tests_completed) {
            summary_stream << test_name << '\n';
        }
        summary_stream << "\nFailed:\n";
        for (const auto& invocation : failed_invocations) {
            summary_stream << invocation << '\n';
        }
        return summary_stream.str();
    }
}; // class GTestRecorder

// Never destroyed, so test_summary() can read it from any thread at any time.
GTestRecorder& GetRecorder() {
    static auto* recorder = new GTestRecorder();
    return *recorder;
}

} // namespace

extern "C" void test_summary(std::string& summary) {
    summary = GetRecorder().Summary();
}

// The caller must not run this function concurrently or more than once per process.
extern "C" int shared_main(int argc, char* argv[], JNIEnv* /*env*/, jobject /*context*/,
                           std::string& messages) {
    ::testing::InitGoogleTest(&argc, argv);

    // Set up result recorder
    UnitTest& unit_test = *UnitTest::GetInstance();
    TestEventListeners& listeners = unit_test.listeners();
    delete listeners.Release(listeners.default_result_printer());

    GTestRecorder& recorder = GetRecorder();

    // Run tests
    int result = 1;
    {
        ScopedTestListener listener_guard(listeners, &recorder);
        result = RUN_ALL_TESTS();
    }
    messages = recorder.GetResult();
    return result;
}
