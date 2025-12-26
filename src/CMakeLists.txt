cmake_minimum_required(VERSION 3.16)

# 新しいポリシーを設定（CMake 3.30+でのBoost検索方法）
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

cmake_policy(SET CMP0144 NEW) # Use new <PACKAGENAME>_ROOT variable behavior

project(hakoniwa_pdu_endpoint LANGUAGES CXX)

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(nlohmann_json)

# システムのBoostを使用（ヘッダーオンリー）
find_package(Boost 1.70 REQUIRED)

add_library(hakoniwa_pdu_endpoint
  socket_utils.cpp
  comm_udp.cpp
  comm_tcp.cpp
  comm_websocket.cpp
  pdu_factory.cpp
  comm_shm.cpp 
  pdu_definition.cpp
)

target_include_directories(hakoniwa_pdu_endpoint
  PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../include
)

# Boostはヘッダーオンリーで使用
target_link_libraries(hakoniwa_pdu_endpoint
  PUBLIC
    nlohmann_json::nlohmann_json
    Boost::headers
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
elseif(UNIX) # Add this block for Linux
  target_include_directories(hakoniwa_pdu_endpoint
    PUBLIC
      /usr/include/hakoniwa
  )
  target_link_libraries(hakoniwa_pdu_endpoint
    PUBLIC
      /usr/lib/x86_64-linux-gnu/libassets.so
  )
endif()

option(HAKO_PDU_ENDPOINT_BUILD_TESTS "Build UDP endpoint tests" ON)

if(HAKO_PDU_ENDPOINT_BUILD_TESTS)
  # enable_testing()
  # add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../test ${CMAKE_CURRENT_BINARY_DIR}/test)
endif()