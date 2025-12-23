cmake_minimum_required(VERSION 3.16)

project(hakoniwa_pdu_endpoint LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(nlohmann_json)

add_library(hakoniwa_pdu_endpoint
  udp_endpoint.cpp
)

target_include_directories(hakoniwa_pdu_endpoint
  PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include
)

target_link_libraries(hakoniwa_pdu_endpoint
  PUBLIC
    nlohmann_json::nlohmann_json
)

option(HAKO_PDU_ENDPOINT_BUILD_TESTS "Build UDP endpoint tests" ON)

if(HAKO_PDU_ENDPOINT_BUILD_TESTS)
  # enable_testing() # この行を削除
  # add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../test ${CMAKE_CURRENT_BINARY_DIR}/test)
endif()
