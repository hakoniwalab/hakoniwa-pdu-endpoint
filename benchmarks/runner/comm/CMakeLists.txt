cmake_minimum_required(VERSION 3.10)

project(runner_comm)

# Find all C++ source files in this directory
file(GLOB_RECURSE SOURCES "*.cpp")

# Create a static library from these sources
add_library(runner_comm STATIC ${SOURCES})

# Specify include directories for this library
target_include_directories(runner_comm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include # for hakoniwa headers
    ${CMAKE_CURRENT_SOURCE_DIR}/../.. # for common (benchmark_config.hpp)
)