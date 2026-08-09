enable_testing()

find_package(GTest REQUIRED)

add_executable(endpoint_test
  endpoint_test.cpp
)

target_link_libraries(endpoint_test
  PRIVATE
    hakoniwa_pdu_endpoint
    GTest::gtest_main
)

if(TARGET hako_pdu_storage_debug)
  add_dependencies(endpoint_test hako_pdu_storage_debug)
  target_compile_definitions(endpoint_test PRIVATE HAKO_PDU_STORAGE_DEBUG_EXE="$<TARGET_FILE:hako_pdu_storage_debug>")
endif()

if(APPLE)
  target_include_directories(endpoint_test
    PRIVATE
      /usr/local/hakoniwa/include/hakoniwa
  )
  target_link_libraries(endpoint_test
    PRIVATE
      /usr/local/hakoniwa/lib/libassets.dylib
  )
endif()

add_test(NAME endpoint_test COMMAND endpoint_test)

add_executable(cache_config_test
  cache_config_test.cpp
)

target_link_libraries(cache_config_test
  PRIVATE
    hakoniwa_pdu_endpoint
    GTest::gtest_main
)

add_test(NAME cache_config_test COMMAND cache_config_test)

add_executable(cache_runtime_config_test
  cache_runtime_config_test.cpp
)

target_link_libraries(cache_runtime_config_test
  PRIVATE
    hakoniwa_pdu_endpoint
    GTest::gtest_main
)

add_test(NAME cache_runtime_config_test COMMAND cache_runtime_config_test)

add_executable(socket_portability_test
  socket_portability_test.cpp
)

target_link_libraries(socket_portability_test
  PRIVATE
    hakoniwa_pdu_endpoint
    GTest::gtest_main
)

add_test(NAME socket_portability_test COMMAND socket_portability_test)

add_executable(tcp_runtime_contract_test
  tcp_runtime_contract_test.cpp
)

target_link_libraries(tcp_runtime_contract_test
  PRIVATE
    hakoniwa_pdu_endpoint
    GTest::gtest_main
)

add_test(NAME tcp_runtime_contract_test COMMAND tcp_runtime_contract_test)

set_tests_properties(endpoint_test PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
