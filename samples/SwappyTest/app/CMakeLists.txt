cmake_minimum_required(VERSION 3.4.1)

project(swappytest)

find_package(games-frame-pacing REQUIRED CONFIG)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20 -Werror -Wall")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384")

include_directories(../../common/include)
include_directories(src/main/cpp)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party)

add_library(${CMAKE_PROJECT_NAME}
            SHARED
            src/main/cpp/CalibrationReport.cpp
            src/main/cpp/CalibrationStats.cpp
            src/main/cpp/CpuCalibration.cpp
            src/main/cpp/CpuGpuFreq.cpp
            src/main/cpp/DvfsBenchmark.cpp
            src/main/cpp/DvfsTrace.cpp
            src/main/cpp/GpuCalibration.cpp
            src/main/cpp/OverlayBatcher.cpp
            src/main/cpp/Scenario.cpp
            src/main/cpp/ScenarioBuilder.cpp
            src/main/cpp/ScenarioEngine.cpp
            src/main/cpp/ScenarioParser.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/json11/json11.cpp
            src/main/cpp/SwappyTestRenderer.cpp
            src/main/cpp/TelemetryRingBuffer.cpp
            src/main/cpp/scenarios/H1.cpp
            src/main/cpp/scenarios/H10.cpp
            src/main/cpp/scenarios/H2.cpp
            src/main/cpp/scenarios/H3.cpp
            src/main/cpp/scenarios/H4.cpp
            src/main/cpp/scenarios/H6.cpp
            src/main/cpp/scenarios/H7.cpp
            src/main/cpp/scenarios/H8.cpp
            src/main/cpp/scenarios/H9.cpp
            src/main/cpp/scenarios/S1.cpp
            src/main/cpp/scenarios/S10.cpp
            src/main/cpp/scenarios/S11.cpp
            src/main/cpp/scenarios/S2.cpp
            src/main/cpp/scenarios/S3.cpp
            src/main/cpp/scenarios/S4.cpp
            src/main/cpp/scenarios/S5.cpp
            src/main/cpp/scenarios/S7.cpp
            src/main/cpp/scenarios/ScenarioCatalog.cpp
            src/main/cpp/gles/GlesCircleRenderer.cpp
            src/main/cpp/gles/GlesContext.cpp
            src/main/cpp/gles/GlesGpuCalibrationTarget.cpp
            src/main/cpp/gles/GlesOverlayRenderer.cpp
            src/main/cpp/vulkan/VulkanBuffer.cpp
            src/main/cpp/vulkan/VulkanCircleRenderer.cpp
            src/main/cpp/vulkan/VulkanContext.cpp
            src/main/cpp/vulkan/VulkanFrameManager.cpp
            src/main/cpp/vulkan/VulkanGpuCalibrationTarget.cpp
            src/main/cpp/vulkan/VulkanOverlayRenderer.cpp
            src/main/cpp/vulkan/VulkanPipeline.cpp
            src/main/cpp/vulkan/VulkanRenderPass.cpp
            src/main/cpp/vulkan/VulkanSwapchain.cpp
            src/main/cpp/vulkan/VulkanUtils.cpp)

target_link_libraries(${CMAKE_PROJECT_NAME}
                      android
                      EGL
                      GLESv2
                      vulkan
                      log
                      games-frame-pacing::swappy_static)
