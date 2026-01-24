cmake_minimum_required(VERSION 3.10)
project(hakoniwa-pdu-endpoint CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# add_subdirectory を使って src と test ディレクトリの CMakeLists.txt を含める
add_subdirectory(src)
enable_testing()
if(HAKO_PDU_ENDPOINT_BUILD_TESTS)
  add_subdirectory(test)
endif()

option(HAKO_PDU_ENDPOINT_BUILD_EXAMPLES "Build example programs" OFF)
if(HAKO_PDU_ENDPOINT_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
