add_executable(endpoint_minimal endpoint_minimal.cpp)
add_executable(endpoint_tcp_server endpoint_tcp_server.cpp)
add_executable(endpoint_tcp_client endpoint_tcp_client.cpp)
add_executable(endpoint_udp_server endpoint_udp_server.cpp)
add_executable(endpoint_udp_client endpoint_udp_client.cpp)
add_executable(endpoint_ws_server endpoint_ws_server.cpp)
add_executable(endpoint_ws_client endpoint_ws_client.cpp)
add_executable(endpoint_tcp_mux endpoint_tcp_mux.cpp)
add_executable(endpoint_internal_cache endpoint_internal_cache.cpp)

set(example_targets
  endpoint_minimal
  endpoint_tcp_server
  endpoint_tcp_client
  endpoint_udp_server
  endpoint_udp_client
  endpoint_ws_server
  endpoint_ws_client
  endpoint_tcp_mux
  endpoint_internal_cache
)

foreach(target_name IN LISTS example_targets)
  target_link_libraries(${target_name}
    PRIVATE
      hakoniwa_pdu_endpoint
  )
endforeach()
