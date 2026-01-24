add_executable(endpoint_minimal endpoint_minimal.cpp)

target_link_libraries(endpoint_minimal
  PRIVATE
    hakoniwa_pdu_endpoint
)
