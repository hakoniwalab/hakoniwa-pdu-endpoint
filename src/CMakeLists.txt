cmake_minimum_required(VERSION 3.16)

# 新しいポリシーを設定（CMake 3.30+でのBoost検索方法）
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

cmake_policy(SET CMP0144 NEW) # Use new <PACKAGENAME>_ROOT variable behavior

project(hakoniwa_pdu_endpoint LANGUAGES CXX)

if(HAKO_PDU_ENDPOINT_ENABLE_ZENOH)
  include(FetchContent)

  file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../ZENOH_VERSION.txt" HAKO_ZENOH_C_VERSION_RAW
       LIMIT_COUNT 1 REGEX "^[^# \t].*")
  if(NOT HAKO_ZENOH_C_VERSION_RAW)
    message(FATAL_ERROR "Failed to read Zenoh version from ZENOH_VERSION.txt")
  endif()
  string(STRIP "${HAKO_ZENOH_C_VERSION_RAW}" HAKO_ZENOH_C_VERSION)

  FetchContent_Declare(
    zenohc
    GIT_REPOSITORY https://github.com/eclipse-zenoh/zenoh-c.git
    GIT_TAG ${HAKO_ZENOH_C_VERSION}
  )
  FetchContent_MakeAvailable(zenohc)
endif()

if(HAKO_PDU_ENDPOINT_ENABLE_MQTT)
  include(FetchContent)

  file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../MQTT_VERSION.txt" HAKO_MQTT_CPP_VERSION_RAW
       LIMIT_COUNT 1 REGEX "^[^# \t].*")
  if(NOT HAKO_MQTT_CPP_VERSION_RAW)
    message(FATAL_ERROR "Failed to read MQTT version from MQTT_VERSION.txt")
  endif()
  string(STRIP "${HAKO_MQTT_CPP_VERSION_RAW}" HAKO_MQTT_CPP_VERSION)

  set(PAHO_WITH_MQTT_C ON CACHE BOOL "" FORCE)
  set(PAHO_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(PAHO_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(PAHO_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
  set(PAHO_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
  set(PAHO_BUILD_TESTS OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    pahomqttcpp
    GIT_REPOSITORY https://github.com/eclipse-paho/paho.mqtt.cpp.git
    GIT_TAG ${HAKO_MQTT_CPP_VERSION}
  )
  FetchContent_MakeAvailable(pahomqttcpp)
endif()

find_package(nlohmann_json 3.11.3 CONFIG QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
  if(DEFINED HAKO_LOCAL_NLOHMANN_JSON_ROOT AND EXISTS "${HAKO_LOCAL_NLOHMANN_JSON_ROOT}/single_include/nlohmann/json.hpp")
    add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
    set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${HAKO_LOCAL_NLOHMANN_JSON_ROOT}/single_include"
    )
    set(HAKO_NLOHMANN_JSON_INCLUDE_DIR "${HAKO_LOCAL_NLOHMANN_JSON_ROOT}/single_include" PARENT_SCOPE)
  else()
    include(FetchContent)

    FetchContent_Declare(
      nlohmann_json
      GIT_REPOSITORY https://github.com/nlohmann/json.git
      GIT_TAG v3.11.3
    )

    FetchContent_GetProperties(nlohmann_json)
    if(NOT nlohmann_json_POPULATED)
      FetchContent_Populate(nlohmann_json)
    endif()

    add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
    set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${nlohmann_json_SOURCE_DIR}/include"
    )

    set(HAKO_NLOHMANN_JSON_INCLUDE_DIR "${nlohmann_json_SOURCE_DIR}/include" PARENT_SCOPE)
  endif()
endif()

# システムのBoostを使用（ヘッダーオンリー）
find_package(Boost 1.70 REQUIRED)

set(SOCKET_PORTABILITY_SRC "")
if(WIN32)
    set(SOCKET_PORTABILITY_SRC socket_portability_win.cpp)
else()
    set(SOCKET_PORTABILITY_SRC socket_portability_posix.cpp)
endif()

add_library(hakoniwa_pdu_endpoint
  c_endpoint.cpp
  socket_utils.cpp
  ${SOCKET_PORTABILITY_SRC}
  comm_udp.cpp
  comm_tcp.cpp
  comm_storage.cpp
  $<$<BOOL:${HAKO_PDU_ENDPOINT_ENABLE_ZENOH}>:comm_zenoh.cpp>
  $<$<BOOL:${HAKO_PDU_ENDPOINT_ENABLE_MQTT}>:comm_mqtt.cpp>
  comm_tcp_mux.cpp
  comm_websocket.cpp
  pdu_factory.cpp
  $<$<BOOL:${HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE}>:comm_shm.cpp>
  $<$<BOOL:${HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE}>:comm_shm_poll.cpp>
  $<$<BOOL:${HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE}>:comm_shm_callback.cpp>
  pdu_definition.cpp
  $<$<BOOL:${HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE}>:hakoniwa_time_source.cpp>
  $<$<BOOL:${HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE}>:hakoniwa_time_source_poll.cpp>
  $<$<BOOL:${HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE}>:hakoniwa_time_source_callback.cpp>
  endpoint_container.cpp
  endpoint_comm_multiplexer.cpp
)

if(BUILD_SHARED_LIBS AND WIN32)
  set_target_properties(hakoniwa_pdu_endpoint PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
  )
endif()

add_library(hakoniwa_pdu_endpoint::hakoniwa_pdu_endpoint ALIAS hakoniwa_pdu_endpoint)

target_include_directories(hakoniwa_pdu_endpoint
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Boostはヘッダーオンリーで使用
target_link_libraries(hakoniwa_pdu_endpoint
  PUBLIC
    nlohmann_json::nlohmann_json
    Boost::headers
)

if(WIN32)
  target_link_libraries(hakoniwa_pdu_endpoint PRIVATE ws2_32)
endif()

if(MSVC)
  target_compile_options(hakoniwa_pdu_endpoint PUBLIC /utf-8)
  target_compile_options(hakoniwa_pdu_endpoint PRIVATE /bigobj)
  target_compile_definitions(hakoniwa_pdu_endpoint PRIVATE _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00)
endif()

if(HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE)
  target_compile_definitions(hakoniwa_pdu_endpoint PUBLIC HAKO_PDU_ENDPOINT_HAS_HAKONIWA_CORE=1)
endif()

if(HAKO_PDU_ENDPOINT_ENABLE_ZENOH)
  if(TARGET zenohc::lib)
    target_link_libraries(hakoniwa_pdu_endpoint PUBLIC zenohc::lib)
  elseif(TARGET zenohc)
    target_link_libraries(hakoniwa_pdu_endpoint PUBLIC zenohc)
  else()
    message(FATAL_ERROR "Zenoh support enabled, but zenoh-c target was not found")
  endif()
  target_compile_definitions(hakoniwa_pdu_endpoint PUBLIC HAKO_PDU_ENDPOINT_HAS_ZENOH=1)
endif()

if(HAKO_PDU_ENDPOINT_ENABLE_MQTT)
  if(TARGET PahoMqttCpp::paho-mqttpp3-static)
    target_link_libraries(hakoniwa_pdu_endpoint PUBLIC PahoMqttCpp::paho-mqttpp3-static)
  elseif(TARGET PahoMqttCpp::paho-mqttpp3)
    target_link_libraries(hakoniwa_pdu_endpoint PUBLIC PahoMqttCpp::paho-mqttpp3)
  elseif(TARGET paho-mqttpp3-static)
    target_link_libraries(hakoniwa_pdu_endpoint PUBLIC paho-mqttpp3-static)
  elseif(TARGET paho-mqttpp3)
    target_link_libraries(hakoniwa_pdu_endpoint PUBLIC paho-mqttpp3)
  else()
    message(FATAL_ERROR "MQTT support enabled, but Paho MQTT C++ target was not found")
  endif()
  target_compile_definitions(hakoniwa_pdu_endpoint PUBLIC HAKO_PDU_ENDPOINT_HAS_MQTT=1)
endif()

# Add OS-specific logic for Hakoniwa core library
if(HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE)
  if(TARGET assets AND TARGET shakoc AND DEFINED HAKO_PDU_ENDPOINT_HAKONIWA_CORE_SOURCE_DIR AND EXISTS "${HAKO_PDU_ENDPOINT_HAKONIWA_CORE_SOURCE_DIR}/include")
    set(HAKO_PDU_ENDPOINT_VENDORED_HAKONIWA_INCLUDES
      $<BUILD_INTERFACE:${HAKO_PDU_ENDPOINT_HAKONIWA_CORE_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${HAKO_PDU_ENDPOINT_HAKONIWA_CORE_SOURCE_DIR}/hakoniwa-pdu-registry/pdu/types>
    )
    if(EXISTS "${HAKO_PDU_ENDPOINT_HAKONIWA_CORE_SOURCE_DIR}/include/hakoniwa")
      list(APPEND HAKO_PDU_ENDPOINT_VENDORED_HAKONIWA_INCLUDES
        $<BUILD_INTERFACE:${HAKO_PDU_ENDPOINT_HAKONIWA_CORE_SOURCE_DIR}/include/hakoniwa>
      )
    endif()
    target_include_directories(hakoniwa_pdu_endpoint PUBLIC
      ${HAKO_PDU_ENDPOINT_VENDORED_HAKONIWA_INCLUDES}
    )
    target_link_libraries(hakoniwa_pdu_endpoint PUBLIC assets shakoc)
  else()
    set(HAKO_HAKONIWA_CORE_ROOT "${HAKO_PDU_ENDPOINT_HAKONIWA_CORE_ROOT}")
    if(HAKO_HAKONIWA_CORE_ROOT STREQUAL "")
      if(DEFINED ENV{HAKONIWA_CORE_ROOT} AND NOT "$ENV{HAKONIWA_CORE_ROOT}" STREQUAL "")
        set(HAKO_HAKONIWA_CORE_ROOT "$ENV{HAKONIWA_CORE_ROOT}")
      elseif(WIN32)
        set(HAKO_HAKONIWA_CORE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../hakoniwa-core-pro/install")
      else()
        set(HAKO_HAKONIWA_CORE_ROOT "/usr/local/hakoniwa")
      endif()
    endif()

    set(HAKONIWA_INCLUDE_DIRS
      "${HAKO_HAKONIWA_CORE_ROOT}/include"
      "${HAKO_HAKONIWA_CORE_ROOT}/include/hakoniwa"
    )
    if(UNIX AND NOT APPLE)
      list(APPEND HAKONIWA_INCLUDE_DIRS /usr/include/hakoniwa)
    endif()
    target_include_directories(hakoniwa_pdu_endpoint PUBLIC ${HAKONIWA_INCLUDE_DIRS})

    set(HAKONIWA_LIB_DIRS "${HAKO_HAKONIWA_CORE_ROOT}/lib")
    if(UNIX AND NOT APPLE)
      list(APPEND HAKONIWA_LIB_DIRS /usr/lib/x86_64-linux-gnu)
    endif()

    set(HAKONIWA_LINK_LIBS "")
    set(HAKONIWA_CORE_LINK_NAMES assets shakoc)
    if(WIN32)
      set(HAKONIWA_CORE_LINK_NAMES assets hako shakoc)
    endif()
    foreach(lib_name IN LISTS HAKONIWA_CORE_LINK_NAMES)
      find_library(HAKONIWA_${lib_name}_LIB NAMES ${lib_name} PATHS ${HAKONIWA_LIB_DIRS})
      if(HAKONIWA_${lib_name}_LIB)
        list(APPEND HAKONIWA_LINK_LIBS ${HAKONIWA_${lib_name}_LIB})
      else()
        list(APPEND HAKONIWA_LINK_LIBS ${lib_name})
      endif()
    endforeach()

    target_link_libraries(hakoniwa_pdu_endpoint PUBLIC ${HAKONIWA_LINK_LIBS})

    if(NOT EXISTS "${HAKO_HAKONIWA_CORE_ROOT}/include")
      message(WARNING "Hakoniwa core include directory not found: ${HAKO_HAKONIWA_CORE_ROOT}/include")
    endif()
  endif()
endif()

option(HAKO_PDU_ENDPOINT_BUILD_TESTS "Build UDP endpoint tests" ON)

if(HAKO_PDU_ENDPOINT_BUILD_TESTS)
  # enable_testing()
  # add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../test ${CMAKE_CURRENT_BINARY_DIR}/test)
endif()
