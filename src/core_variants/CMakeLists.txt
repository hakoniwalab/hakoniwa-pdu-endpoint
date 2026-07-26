# Additional Hakoniwa Core endpoint variants.
#
# The legacy hakoniwa_pdu_endpoint target is intentionally left untouched.
# These variants provide an explicit Core integration boundary so consumers can
# depend on either the callback/assets API or the polling/shakoc API without
# linking both Core frontends into the same endpoint library.

set(HAKO_ENDPOINT_VARIANT_SOCKET_SRC "../socket_portability_posix.cpp")
if(WIN32)
  set(HAKO_ENDPOINT_VARIANT_SOCKET_SRC "../socket_portability_win.cpp")
endif()

set(HAKO_ENDPOINT_VARIANT_COMMON_SRC
  ../c_endpoint.cpp
  ../socket_utils.cpp
  ${HAKO_ENDPOINT_VARIANT_SOCKET_SRC}
  ../comm_udp.cpp
  ../comm_tcp.cpp
  ../comm_storage.cpp
  ../comm_tcp_mux.cpp
  ../comm_websocket.cpp
  ../pdu_factory.cpp
  ../comm_shm.cpp
  ../pdu_definition.cpp
  ../hakoniwa_time_source.cpp
  ../endpoint_container.cpp
  ../endpoint_comm_multiplexer.cpp
)

if(HAKO_PDU_ENDPOINT_ENABLE_ZENOH)
  list(APPEND HAKO_ENDPOINT_VARIANT_COMMON_SRC
    ../comm_zenoh.cpp
    ../comm_rmw_zenoh.cpp
  )
endif()

if(HAKO_PDU_ENDPOINT_ENABLE_MQTT)
  list(APPEND HAKO_ENDPOINT_VARIANT_COMMON_SRC ../comm_mqtt.cpp)
endif()

# src/CMakeLists.txt owns the common dependency resolution. This directory is a
# child of src so imported targets and resolved variables are visible here.
if(NOT HAKO_PDU_ENDPOINT_BOOST_TARGET)
  message(FATAL_ERROR "Hakoniwa Core endpoint variants require a resolved Boost target")
endif()
if(NOT TARGET nlohmann_json::nlohmann_json)
  message(FATAL_ERROR "Hakoniwa Core endpoint variants require nlohmann_json::nlohmann_json")
endif()

# Resolve the two mutually-exclusive Core frontend link targets. In an in-tree
# build, use the native Core targets. For an installed Core, consume its CMake
# package instead of reconstructing include/library paths manually.
if(TARGET assets AND TARGET shakoc)
  set(HAKO_ENDPOINT_CALLBACK_CORE_TARGET assets)
  set(HAKO_ENDPOINT_POLLING_CORE_TARGET shakoc)
else()
  set(HAKO_ENDPOINT_VARIANT_CORE_ROOT "${HAKO_PDU_ENDPOINT_HAKONIWA_CORE_ROOT}")
  if(HAKO_ENDPOINT_VARIANT_CORE_ROOT STREQUAL "")
    if(DEFINED ENV{HAKONIWA_CORE_ROOT} AND NOT "$ENV{HAKONIWA_CORE_ROOT}" STREQUAL "")
      set(HAKO_ENDPOINT_VARIANT_CORE_ROOT "$ENV{HAKONIWA_CORE_ROOT}")
    elseif(WIN32)
      set(HAKO_ENDPOINT_VARIANT_CORE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../hakoniwa-core-pro/install")
    else()
      set(HAKO_ENDPOINT_VARIANT_CORE_ROOT "/usr/local/hakoniwa")
    endif()
  endif()

  find_package(hakoniwa-core CONFIG REQUIRED
    PATHS "${HAKO_ENDPOINT_VARIANT_CORE_ROOT}"
    NO_DEFAULT_PATH
  )

  if(NOT TARGET hakoniwa-core::assets)
    message(FATAL_ERROR "Hakoniwa Core package does not export hakoniwa-core::assets")
  endif()
  if(NOT TARGET hakoniwa-core::shakoc)
    message(FATAL_ERROR "Hakoniwa Core package does not export hakoniwa-core::shakoc")
  endif()

  set(HAKO_ENDPOINT_CALLBACK_CORE_TARGET hakoniwa-core::assets)
  set(HAKO_ENDPOINT_POLLING_CORE_TARGET hakoniwa-core::shakoc)
endif()

function(hako_configure_core_endpoint_variant target_name export_name)
  target_include_directories(${target_name}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  target_link_libraries(${target_name}
    PUBLIC
      nlohmann_json::nlohmann_json
      ${HAKO_PDU_ENDPOINT_BOOST_TARGET}
  )

  set_target_properties(${target_name} PROPERTIES EXPORT_NAME ${export_name})
  target_compile_definitions(${target_name} PUBLIC HAKO_PDU_ENDPOINT_HAS_HAKONIWA_CORE=1)

  if(WIN32)
    target_link_libraries(${target_name} PRIVATE ws2_32)
  endif()

  if(MSVC)
    target_compile_options(${target_name} PUBLIC /utf-8)
    target_compile_options(${target_name} PRIVATE /bigobj)
    target_compile_definitions(${target_name} PRIVATE _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00)
  endif()

  if(HAKO_PDU_ENDPOINT_ENABLE_ZENOH)
    if(TARGET zenohc::static)
      target_link_libraries(${target_name} PUBLIC zenohc::static)
    elseif(TARGET zenohc::lib)
      target_link_libraries(${target_name} PUBLIC zenohc::lib)
    elseif(TARGET zenohc)
      target_link_libraries(${target_name} PUBLIC zenohc)
    else()
      message(FATAL_ERROR "Zenoh support enabled, but zenoh-c target was not found")
    endif()
    target_compile_definitions(${target_name} PUBLIC HAKO_PDU_ENDPOINT_HAS_ZENOH=1)
  endif()

  if(HAKO_PDU_ENDPOINT_ENABLE_MQTT)
    if(TARGET PahoMqttCpp::paho-mqttpp3-static)
      target_link_libraries(${target_name} PUBLIC PahoMqttCpp::paho-mqttpp3-static)
    elseif(TARGET PahoMqttCpp::paho-mqttpp3)
      target_link_libraries(${target_name} PUBLIC PahoMqttCpp::paho-mqttpp3)
    elseif(TARGET paho-mqttpp3-static)
      target_link_libraries(${target_name} PUBLIC paho-mqttpp3-static)
    elseif(TARGET paho-mqttpp3)
      target_link_libraries(${target_name} PUBLIC paho-mqttpp3)
    else()
      message(FATAL_ERROR "MQTT support enabled, but Paho MQTT C++ target was not found")
    endif()
    target_compile_definitions(${target_name} PUBLIC HAKO_PDU_ENDPOINT_HAS_MQTT=1)
  endif()
endfunction()

add_library(hakoniwa_pdu_endpoint_core_callback
  ${HAKO_ENDPOINT_VARIANT_COMMON_SRC}
  ../comm_shm_callback.cpp
  comm_shm_poll_unavailable.cpp
  ../hakoniwa_time_source_callback.cpp
  hakoniwa_time_source_poll_unavailable.cpp
)
hako_configure_core_endpoint_variant(hakoniwa_pdu_endpoint_core_callback core_callback)
target_link_libraries(hakoniwa_pdu_endpoint_core_callback PUBLIC ${HAKO_ENDPOINT_CALLBACK_CORE_TARGET})
add_library(hakoniwa_pdu_endpoint::core_callback ALIAS hakoniwa_pdu_endpoint_core_callback)

add_library(hakoniwa_pdu_endpoint_core_polling
  ${HAKO_ENDPOINT_VARIANT_COMMON_SRC}
  ../comm_shm_poll.cpp
  comm_shm_callback_unavailable.cpp
  ../hakoniwa_time_source_poll.cpp
  hakoniwa_time_source_callback_unavailable.cpp
)
hako_configure_core_endpoint_variant(hakoniwa_pdu_endpoint_core_polling core_polling)
target_link_libraries(hakoniwa_pdu_endpoint_core_polling PUBLIC ${HAKO_ENDPOINT_POLLING_CORE_TARGET})
add_library(hakoniwa_pdu_endpoint::core_polling ALIAS hakoniwa_pdu_endpoint_core_polling)

message(STATUS "Hakoniwa Core endpoint variants enabled: core_callback, core_polling")
