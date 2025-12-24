enable_testing()

find_package(GTest REQUIRED)

add_executable(udp_endpoint_test
  udp_endpoint_test.cpp
)

target_link_libraries(udp_endpoint_test
  PRIVATE
    hakoniwa_pdu_endpoint
    GTest::gtest_main
)

# include(GoogleTest) # この行は不要になる可能性があるためコメントアウト
# gtest_discover_tests(udp_endpoint_test) # この行を削除

# add_test を使ってテスト実行ファイルを登録
add_test(NAME udp_endpoint_test COMMAND udp_endpoint_test)

add_executable(tcp_endpoint_test
  tcp_endpoint_test.cpp
)

target_link_libraries(tcp_endpoint_test
  PRIVATE
    hakoniwa_pdu_endpoint
    GTest::gtest_main
)

add_test(NAME tcp_endpoint_test COMMAND tcp_endpoint_test)

add_executable(buffer_smart_endpoint_test
  buffer_smart_endpoint_test.cpp
)

target_link_libraries(buffer_smart_endpoint_test
  PRIVATE
    hakoniwa_pdu_endpoint
    GTest::gtest_main
)

add_test(NAME buffer_smart_endpoint_test COMMAND buffer_smart_endpoint_test)
set_tests_properties(buffer_smart_endpoint_test PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
