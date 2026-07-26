cmake_minimum_required(VERSION 3.16)
project(hakoniwa_pdu_endpoint_package_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(hakoniwa_pdu_endpoint CONFIG REQUIRED)

add_executable(endpoint_core_callback_consumer main.cpp)
target_link_libraries(endpoint_core_callback_consumer PRIVATE
  hakoniwa_pdu_endpoint::core_callback
)

add_executable(endpoint_core_polling_consumer main.cpp)
target_link_libraries(endpoint_core_polling_consumer PRIVATE
  hakoniwa_pdu_endpoint::core_polling
)
