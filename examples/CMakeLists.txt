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

find_package(fastcdr CONFIG QUIET)
if(fastcdr_FOUND)
  add_executable(endpoint_zenoh_pub_cdr endpoint_zenoh_pub_cdr.cpp)
  add_executable(endpoint_zenoh_sub_cdr endpoint_zenoh_sub_cdr.cpp)
  target_include_directories(endpoint_zenoh_pub_cdr PRIVATE ${CMAKE_CURRENT_LIST_DIR}/cdr)
  target_include_directories(endpoint_zenoh_sub_cdr PRIVATE ${CMAKE_CURRENT_LIST_DIR}/cdr)
  target_link_libraries(endpoint_zenoh_pub_cdr PRIVATE fastcdr)
  target_link_libraries(endpoint_zenoh_sub_cdr PRIVATE fastcdr)
else()
  message(STATUS "fastcdr was not found; skipping CDR example programs")
endif()

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

if(fastcdr_FOUND)
  list(APPEND example_targets
    endpoint_zenoh_pub_cdr
    endpoint_zenoh_sub_cdr
  )
endif()

if(HAKO_PDU_ENDPOINT_ENABLE_MQTT)
  add_executable(endpoint_mqtt_pub endpoint_mqtt_pub.cpp)
  add_executable(endpoint_mqtt_sub endpoint_mqtt_sub.cpp)
  list(APPEND example_targets
    endpoint_mqtt_pub
    endpoint_mqtt_sub
  )
endif()

foreach(target_name IN LISTS example_targets)
  target_link_libraries(${target_name}
    PRIVATE
      hakoniwa_pdu_endpoint
  )
endforeach()
