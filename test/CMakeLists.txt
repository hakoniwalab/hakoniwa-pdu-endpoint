add_executable(udp_endpoint_test
  udp_endpoint_test.cpp
)

target_link_libraries(udp_endpoint_test
  PRIVATE
    hakoniwa_pdu_endpoint
)

add_test(NAME udp_endpoint_test COMMAND udp_endpoint_test)
