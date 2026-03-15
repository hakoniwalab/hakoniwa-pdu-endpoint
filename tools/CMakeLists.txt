add_executable(hako_pdu_storage_debug
  storage_debug.cpp
)

target_link_libraries(hako_pdu_storage_debug
  PRIVATE
    hakoniwa_pdu_endpoint
)
