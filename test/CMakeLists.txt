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

# 設定ファイルをテスト実行時にワーキングディレクトリにコピー
add_custom_command(TARGET buffer_smart_endpoint_test POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy
                           ${CMAKE_CURRENT_SOURCE_DIR}/latest_config.json
                           $<TARGET_FILE_DIR:buffer_smart_endpoint_test>/latest_config.json
                   COMMAND ${CMAKE_COMMAND} -E copy
                           ${CMAKE_CURRENT_SOURCE_DIR}/queue_config.json
                           $<TARGET_FILE_DIR:buffer_smart_endpoint_test>/queue_config.json
                   COMMAND ${CMAKE_COMMAND} -E copy
                           ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
                           $<TARGET_FILE_DIR:buffer_smart_endpoint_test>/CMakeLists.txt
)
add_test(NAME buffer_smart_endpoint_test COMMAND buffer_smart_endpoint_test)
