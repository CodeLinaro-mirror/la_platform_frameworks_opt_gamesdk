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

cmake_minimum_required(VERSION 3.10)

project(swappy_host_test CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ANDROID_GTEST_DIR "../../../external/googletest")
set(BUILD_GMOCK OFF)
set(INSTALL_GTEST OFF)
set(gtest_disable_pthreads ON CACHE BOOL "" FORCE)

add_subdirectory("${ANDROID_GTEST_DIR}" googletest-build)

include_directories(
  "${ANDROID_GTEST_DIR}/googletest/include"
  ../../games-frame-pacing/common
)

add_executable(swappy_host_test
  swappy_host_test.cpp
  ../../games-frame-pacing/common/Settings.cpp
)

target_link_libraries(swappy_host_test
  gtest
  gtest_main
  pthread
)
