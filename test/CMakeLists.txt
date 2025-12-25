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

# Set the working directory to the project root so tests can find the config files
set_tests_properties(endpoint_test PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
