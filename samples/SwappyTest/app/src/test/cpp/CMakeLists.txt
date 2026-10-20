#
# Copyright 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

cmake_minimum_required(VERSION 3.18.1)

project(swappytest_host_test CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../../external/googletest")
  set(ANDROID_GTEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../../external/googletest")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../../../../external/googletest")
  set(ANDROID_GTEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../../../../external/googletest")
else()
  message(FATAL_ERROR "googletest directory not found")
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_GMOCK OFF)
set(INSTALL_GTEST OFF)

find_package(Threads REQUIRED)

add_subdirectory("${ANDROID_GTEST_DIR}" googletest-build)

enable_testing()

add_executable(swappytest_host_test
  calibration_stats_test.cpp
  scenario_builder_test.cpp
  scenario_catalog_test.cpp
  scenario_engine_test.cpp
  scenario_parser_test.cpp
  swappytest_host_test.cpp
  telemetry_ring_buffer_test.cpp
  ../../main/cpp/CalibrationStats.cpp
  ../../main/cpp/OverlayBatcher.cpp
  ../../main/cpp/Scenario.cpp
  ../../main/cpp/ScenarioBuilder.cpp
  ../../main/cpp/ScenarioEngine.cpp
  ../../main/cpp/ScenarioParser.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../third_party/json11/json11.cpp
  ../../main/cpp/scenarios/H1.cpp
  ../../main/cpp/scenarios/H10.cpp
  ../../main/cpp/scenarios/H2.cpp
  ../../main/cpp/scenarios/H3.cpp
  ../../main/cpp/scenarios/H4.cpp
  ../../main/cpp/scenarios/H6.cpp
  ../../main/cpp/scenarios/H7.cpp
  ../../main/cpp/scenarios/H8.cpp
  ../../main/cpp/scenarios/H9.cpp
  ../../main/cpp/scenarios/S1.cpp
  ../../main/cpp/scenarios/S10.cpp
  ../../main/cpp/scenarios/S11.cpp
  ../../main/cpp/scenarios/S2.cpp
  ../../main/cpp/scenarios/S3.cpp
  ../../main/cpp/scenarios/S4.cpp
  ../../main/cpp/scenarios/S5.cpp
  ../../main/cpp/scenarios/S7.cpp
  ../../main/cpp/scenarios/ScenarioCatalog.cpp
  ../../main/cpp/TelemetryRingBuffer.cpp
)

target_include_directories(swappytest_host_test PRIVATE
  include
  ../../main/cpp
  ../../../../../common/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../third_party
)

target_include_directories(swappytest_host_test SYSTEM PRIVATE
  "${ANDROID_GTEST_DIR}/googletest/include"
)

target_compile_options(swappytest_host_test PRIVATE
  -Wall
  -Wextra
  -Werror
  -UNDEBUG
)

target_link_libraries(swappytest_host_test PRIVATE
  gtest
  gtest_main
  Threads::Threads
)

add_test(NAME swappytest_host_test COMMAND swappytest_host_test)
