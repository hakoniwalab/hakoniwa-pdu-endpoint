cmake_minimum_required(VERSION 3.10)

project(benchmark_runners)

set(BENCHMARK_RUNNER_EXTRA_LIBS "")
set(BENCHMARK_RUNNER_EXTRA_INCLUDES "")

if(HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE)
    set(HAKO_BENCHMARK_HAKONIWA_CORE_ROOT "${HAKO_PDU_ENDPOINT_HAKONIWA_CORE_ROOT}")
    if(HAKO_BENCHMARK_HAKONIWA_CORE_ROOT STREQUAL "")
        if(DEFINED ENV{HAKONIWA_CORE_ROOT} AND NOT "$ENV{HAKONIWA_CORE_ROOT}" STREQUAL "")
            set(HAKO_BENCHMARK_HAKONIWA_CORE_ROOT "$ENV{HAKONIWA_CORE_ROOT}")
        elseif(WIN32)
            set(HAKO_BENCHMARK_HAKONIWA_CORE_ROOT "${CMAKE_SOURCE_DIR}/../hakoniwa-core-pro/install")
        else()
            set(HAKO_BENCHMARK_HAKONIWA_CORE_ROOT "/usr/local/hakoniwa")
        endif()
    endif()

    find_library(HAKONIWA_ASSETS_LIBRARY
        NAMES assets assets.1.0.0
        PATHS "${HAKO_BENCHMARK_HAKONIWA_CORE_ROOT}/lib"
        REQUIRED
    )

    find_library(HAKONIWA_CONDUCTOR_LIBRARY
        NAMES conductor conductor.1.0.0
        PATHS "${HAKO_BENCHMARK_HAKONIWA_CORE_ROOT}/lib"
        REQUIRED
    )

    list(APPEND BENCHMARK_RUNNER_EXTRA_LIBS
        ${HAKONIWA_ASSETS_LIBRARY}
        ${HAKONIWA_CONDUCTOR_LIBRARY}
    )
    list(APPEND BENCHMARK_RUNNER_EXTRA_INCLUDES
        "${HAKO_BENCHMARK_HAKONIWA_CORE_ROOT}/include"
        "${HAKO_BENCHMARK_HAKONIWA_CORE_ROOT}/include/hakoniwa"
    )
endif()

include_directories(
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/benchmarks"
    ${BENCHMARK_RUNNER_EXTRA_INCLUDES}
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
        ${BENCHMARK_RUNNER_EXTRA_LIBS}
)

target_link_libraries(benchmark_sub_runner
    PRIVATE
        runner_comm
        hakoniwa_pdu_endpoint
        ${BENCHMARK_RUNNER_EXTRA_LIBS}
)

if(MSVC)
    target_compile_options(benchmark_pub_runner PRIVATE /utf-8)
    target_compile_options(benchmark_sub_runner PRIVATE /utf-8)
endif()
