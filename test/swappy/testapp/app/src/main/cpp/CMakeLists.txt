#
# Copyright 2020 The Android Open Source Project
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
project(swappy-test)

set (TEST_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../..")

set( CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384" )

add_subdirectory( "${TEST_DIR}" test_dir)

include_directories("${TEST_DIR}"
        "${TEST_DIR}/../../samples/common/include"
        "${TEST_DIR}/../../games-frame-pacing"
        "${TEST_DIR}/../../games-frame-pacing/common"
        "${TEST_DIR}/../../src/common"
        )

add_library( ${PROJECT_NAME} SHARED
             swappy-test.cpp )

target_link_libraries( ${PROJECT_NAME}
                       swappy_test_lib
                       log )

find_package(googletest REQUIRED CONFIG)
find_package(junit-gtest REQUIRED CONFIG)
find_package(games-frame-pacing REQUIRED CONFIG)

add_library(swappy_instrumented_test SHARED
  ${TEST_DIR}/swappycommon_test.cpp
  ${TEST_DIR}/swappy_basic_test.cpp
  ${TEST_DIR}/../../src/common/system_utils.cpp
)
target_link_libraries(swappy_instrumented_test
  PRIVATE
    games-frame-pacing::swappy
    android
    atomic
    log
    vulkan
    EGL
    GLESv2
    mediandk
    googletest::gtest
    googletest::gmock
    junit-gtest::junit-gtest
)
