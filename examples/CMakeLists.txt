add_executable(endpoint_minimal endpoint_minimal.cpp)
add_executable(endpoint_tcp_server endpoint_tcp_server.cpp)
add_executable(endpoint_tcp_client endpoint_tcp_client.cpp)
add_executable(endpoint_udp_server endpoint_udp_server.cpp)
add_executable(endpoint_udp_client endpoint_udp_client.cpp)
add_executable(endpoint_ws_server endpoint_ws_server.cpp)
add_executable(endpoint_ws_client endpoint_ws_client.cpp)
add_executable(endpoint_tcp_mux endpoint_tcp_mux.cpp)
add_executable(endpoint_internal_cache endpoint_internal_cache.cpp)
add_executable(endpoint_storage_queue endpoint_storage_queue.cpp)
add_executable(endpoint_storage_latest endpoint_storage_latest.cpp)
add_executable(endpoint_zenoh_pub endpoint_zenoh_pub.cpp)
add_executable(endpoint_zenoh_sub endpoint_zenoh_sub.cpp)

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
  endpoint_storage_queue
  endpoint_storage_latest
  endpoint_zenoh_pub
  endpoint_zenoh_sub
)

foreach(target_name IN LISTS example_targets)
  target_link_libraries(${target_name}
    PRIVATE
      hakoniwa_pdu_endpoint
  )
endforeach()
