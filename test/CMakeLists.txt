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
