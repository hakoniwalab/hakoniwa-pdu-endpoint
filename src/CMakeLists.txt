cmake_minimum_required(VERSION 3.16)

project(hakoniwa_pdu_endpoint LANGUAGES CXX)

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(nlohmann_json)

add_library(hakoniwa_pdu_endpoint
  socket_utils.cpp
  comm_udp.cpp
  comm_tcp.cpp
  pdu_factory.cpp
  comm_shm.cpp 
  pdu_definition.cpp
)

target_include_directories(hakoniwa_pdu_endpoint
  PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include
)

target_link_libraries(hakoniwa_pdu_endpoint
  PUBLIC
    nlohmann_json::nlohmann_json
)

# Add OS-specific logic for Hakoniwa core library
if(APPLE)
  target_include_directories(hakoniwa_pdu_endpoint
    PUBLIC
      /usr/local/hakoniwa/include/hakoniwa
  )
  target_link_libraries(hakoniwa_pdu_endpoint
    PUBLIC
      /usr/local/hakoniwa/lib/libassets.dylib
  )
endif()


option(HAKO_PDU_ENDPOINT_BUILD_TESTS "Build UDP endpoint tests" ON)

if(HAKO_PDU_ENDPOINT_BUILD_TESTS)
  # enable_testing() # この行を削除
  # add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../test ${CMAKE_CURRENT_BINARY_DIR}/test)
endif()
