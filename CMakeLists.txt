cmake_minimum_required(VERSION 3.15)
project(BMonitor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED ENV{ANDROID_NDK})
    message(FATAL_ERROR "ANDROID_NDK environment variable is not set")
endif()

set(ANDROID_NDK $ENV{ANDROID_NDK})
set(CMAKE_CXX_COMPILER ${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang++)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Ofast -flto")


add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    lib/json/include
)

add_executable(${PROJECT_NAME}
    src/monitors.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    nlohmann_json
    -static-libstdc++
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
