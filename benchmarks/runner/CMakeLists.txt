cmake_minimum_required(VERSION 3.10)

project(benchmark_runners)

find_library(HAKONIWA_ASSETS_LIBRARY
    NAMES assets assets.1.0.0
    HINTS /usr/local/hakoniwa/lib
    REQUIRED
)

find_library(HAKONIWA_CONDUCTOR_LIBRARY
    NAMES conductor conductor.1.0.0
    HINTS /usr/local/hakoniwa/lib
    REQUIRED
)

include_directories(
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/benchmarks"
    "/usr/local/hakoniwa/include/hakoniwa/pdu"
)

add_subdirectory(comm)

add_executable(benchmark_pub_runner
    benchmark_pub_runner.cpp
)
add_executable(benchmark_sub_runner
    benchmark_sub_runner.cpp
)


# Link the library against the communication library and the main pdu endpoint library

target_link_libraries(benchmark_pub_runner
    PRIVATE
        runner_comm
        hakoniwa_pdu_endpoint
        ${HAKONIWA_ASSETS_LIBRARY}
)

target_link_libraries(benchmark_sub_runner
    PRIVATE
        runner_comm
        hakoniwa_pdu_endpoint
        ${HAKONIWA_ASSETS_LIBRARY}
        ${HAKONIWA_CONDUCTOR_LIBRARY}
)
