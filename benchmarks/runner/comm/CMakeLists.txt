cmake_minimum_required(VERSION 3.10)

project(runner_comm)

# Find all C++ source files in this directory
file(GLOB_RECURSE SOURCES "*.cpp")

if(NOT HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE)
    list(FILTER SOURCES EXCLUDE REGEX ".*/(pub_runner_shm|sub_runner_shm|shm_runner)\\.cpp$")
endif()

# Create a static library from these sources
add_library(runner_comm STATIC ${SOURCES})


# Specify include directories for this library
target_include_directories(runner_comm PUBLIC
    "${CMAKE_SOURCE_DIR}/include" # for hakoniwa headers
    "${CMAKE_SOURCE_DIR}/benchmarks" # for common (benchmark_config.hpp)
)
target_link_libraries(runner_comm PRIVATE hakoniwa_pdu_endpoint)

if(MSVC)
    target_compile_options(runner_comm PRIVATE /utf-8)
endif()
