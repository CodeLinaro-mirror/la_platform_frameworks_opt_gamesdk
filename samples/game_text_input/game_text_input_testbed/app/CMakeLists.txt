cmake_minimum_required(VERSION 3.10.0)
set(CMAKE_CXX_STANDARD 17)

project(game-input)

include_directories(src/main/cpp)

set( CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384" )

# Find the Game Text Input package, exposed via Prefab.
find_package(game-text-input REQUIRED CONFIG)

add_library(${CMAKE_PROJECT_NAME} SHARED src/main/cpp/game-input.cpp)

target_link_libraries(${CMAKE_PROJECT_NAME}
        game-text-input::game-text-input
        android
        log)
