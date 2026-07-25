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

# Add OS-specific logic for Hakoniwa core library for the test target
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

# Set the working directory to the project root so tests can find the config files
set_tests_properties(endpoint_test PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
