add_executable(hako_pdu_storage_debug
  storage_debug.cpp
)

target_link_libraries(hako_pdu_storage_debug
  PRIVATE
    hakoniwa_pdu_endpoint
)

if(MSVC)
  target_compile_definitions(hako_pdu_storage_debug PRIVATE _WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS)
endif()
