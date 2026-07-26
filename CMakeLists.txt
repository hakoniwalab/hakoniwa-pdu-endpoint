cmake_minimum_required(VERSION 3.10)
project(hakoniwa-pdu-endpoint CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

set(HAKONIWA_PDU_ENDPOINT_VERSION 1.0.0)
option(BUILD_SHARED_LIBS "Build shared libraries instead of static libraries" OFF)

option(HAKO_PDU_ENDPOINT_ENABLE_ZENOH "Enable Zenoh transport support via zenoh-c" OFF)
option(HAKO_PDU_ENDPOINT_ENABLE_MQTT "Enable MQTT transport support via Eclipse Paho MQTT C++" OFF)
option(HAKO_PDU_ENDPOINT_INSTALL "Enable install/export rules" ON)
if(WIN32)
  option(HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE "Enable Hakoniwa core integration (SHM/time source)" OFF)
else()
  option(HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE "Enable Hakoniwa core integration (SHM/time source)" ON)
endif()
set(HAKO_PDU_ENDPOINT_HAKONIWA_CORE_ROOT "" CACHE PATH "Hakoniwa core install root (contains include/ and lib/)")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# When Endpoint is configured against an installed Hakoniwa Core and its targets
# will be exported, resolve the Core package before entering src/. Imported
# targets are directory-scoped in CMake, so creating them here first makes them
# visible to src/ and its core_variants child during generate-time validation.
set(_hako_endpoint_external_core_package FALSE)
if(HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE AND HAKO_PDU_ENDPOINT_INSTALL AND
   NOT (TARGET assets AND TARGET shakoc))
  set(_hako_endpoint_core_root "${HAKO_PDU_ENDPOINT_HAKONIWA_CORE_ROOT}")
  if(_hako_endpoint_core_root STREQUAL "")
    if(DEFINED ENV{HAKONIWA_CORE_ROOT} AND NOT "$ENV{HAKONIWA_CORE_ROOT}" STREQUAL "")
      set(_hako_endpoint_core_root "$ENV{HAKONIWA_CORE_ROOT}")
    elseif(WIN32)
      set(_hako_endpoint_core_root "${CMAKE_CURRENT_LIST_DIR}/../hakoniwa-core-pro/install")
    else()
      set(_hako_endpoint_core_root "/usr/local/hakoniwa")
    endif()
  endif()

  find_package(hakoniwa-core CONFIG REQUIRED
    PATHS "${_hako_endpoint_core_root}"
    NO_DEFAULT_PATH
  )
  set(_hako_endpoint_external_core_package TRUE)
endif()

# add_subdirectory を使って src と test ディレクトリの CMakeLists.txt を含める
add_subdirectory(src)

# The legacy target historically resolves an installed Hakoniwa Core with raw
# include/library paths. Keep that build behavior for compatibility, but make
# its exported usage requirements relocatable by expressing the public Core
# dependency through the installed Core package targets.
if(_hako_endpoint_external_core_package)
  get_target_property(_hako_endpoint_interface_includes
    hakoniwa_pdu_endpoint INTERFACE_INCLUDE_DIRECTORIES)
  if(_hako_endpoint_interface_includes)
    set(_hako_endpoint_export_includes "")
    foreach(_include IN LISTS _hako_endpoint_interface_includes)
      string(FIND "${_include}" "${_hako_endpoint_core_root}/include" _core_include_pos)
      if(NOT _core_include_pos EQUAL 0)
        list(APPEND _hako_endpoint_export_includes "${_include}")
      endif()
    endforeach()
    set_property(TARGET hakoniwa_pdu_endpoint PROPERTY
      INTERFACE_INCLUDE_DIRECTORIES "${_hako_endpoint_export_includes}")
  endif()

  get_target_property(_hako_endpoint_interface_links
    hakoniwa_pdu_endpoint INTERFACE_LINK_LIBRARIES)
  set(_hako_endpoint_export_links "")
  foreach(_link IN LISTS _hako_endpoint_interface_links)
    string(FIND "${_link}" "${_hako_endpoint_core_root}/" _core_link_pos)
    if(_core_link_pos EQUAL 0 OR
       _link STREQUAL "assets" OR _link STREQUAL "shakoc" OR _link STREQUAL "hako")
      continue()
    endif()
    list(APPEND _hako_endpoint_export_links "${_link}")
  endforeach()
  list(APPEND _hako_endpoint_export_links
    hakoniwa-core::assets
    hakoniwa-core::shakoc
  )
  set_property(TARGET hakoniwa_pdu_endpoint PROPERTY
    INTERFACE_LINK_LIBRARIES "${_hako_endpoint_export_links}")
endif()

option(HAKO_PDU_ENDPOINT_BUILD_TOOLS "Build debug and inspection tools" ON)
if(HAKO_PDU_ENDPOINT_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

enable_testing()
if(HAKO_PDU_ENDPOINT_BUILD_TESTS)
  add_subdirectory(test)
endif()

set(HAKO_PDU_ENDPOINT_BUILD_BENCHMARKS_DEFAULT ON)
option(HAKO_PDU_ENDPOINT_BUILD_BENCHMARKS "Build benchmark programs" ${HAKO_PDU_ENDPOINT_BUILD_BENCHMARKS_DEFAULT})
if(HAKO_PDU_ENDPOINT_BUILD_BENCHMARKS)
  if(WIN32 AND NOT HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE)
    message(STATUS "Building TCP/UDP benchmarks on Windows (SHM benchmarks disabled)")
  elseif(NOT HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE)
    message(STATUS "Building TCP/UDP benchmarks without Hakoniwa core support (SHM benchmarks disabled)")
  endif()
  add_subdirectory(benchmarks)
endif()

option(HAKO_PDU_ENDPOINT_BUILD_EXAMPLES "Build example programs" OFF)
if(HAKO_PDU_ENDPOINT_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

set(hakoniwa_pdu_endpoint_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/hakoniwa_pdu_endpoint")
set(HAKO_PDU_ENDPOINT_PACKAGE_HAS_HAKONIWA_CORE ${HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE})

if(HAKO_PDU_ENDPOINT_INSTALL)
  install(
    TARGETS hakoniwa_pdu_endpoint
    EXPORT hakoniwa_pdu_endpointTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  if(HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE)
    install(
      TARGETS
        hakoniwa_pdu_endpoint_core_callback
        hakoniwa_pdu_endpoint_core_polling
      EXPORT hakoniwa_pdu_endpointTargets
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
  endif()

  install(
    DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  if(HAKO_NLOHMANN_JSON_INCLUDE_DIR AND EXISTS "${HAKO_NLOHMANN_JSON_INCLUDE_DIR}/nlohmann")
    install(
      DIRECTORY "${HAKO_NLOHMANN_JSON_INCLUDE_DIR}/nlohmann"
      DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
  endif()

  configure_package_config_file(
    ${CMAKE_CURRENT_LIST_DIR}/cmake/hakoniwa_pdu_endpointConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/hakoniwa_pdu_endpointConfig.cmake
    INSTALL_DESTINATION ${hakoniwa_pdu_endpoint_cmake_dir}
  )

  configure_package_config_file(
    ${CMAKE_CURRENT_LIST_DIR}/cmake/HakoniwaPduEndpointConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/HakoniwaPduEndpointConfig.cmake
    INSTALL_DESTINATION ${hakoniwa_pdu_endpoint_cmake_dir}
  )

  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/hakoniwa_pdu_endpointConfigVersion.cmake
    VERSION ${HAKONIWA_PDU_ENDPOINT_VERSION}
    COMPATIBILITY SameMajorVersion
  )

  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/HakoniwaPduEndpointConfigVersion.cmake
    VERSION ${HAKONIWA_PDU_ENDPOINT_VERSION}
    COMPATIBILITY SameMajorVersion
  )

  install(
    EXPORT hakoniwa_pdu_endpointTargets
    FILE hakoniwa_pdu_endpointTargets.cmake
    NAMESPACE hakoniwa_pdu_endpoint::
    DESTINATION ${hakoniwa_pdu_endpoint_cmake_dir}
  )

  install(
    FILES
      ${CMAKE_CURRENT_BINARY_DIR}/hakoniwa_pdu_endpointConfig.cmake
      ${CMAKE_CURRENT_BINARY_DIR}/hakoniwa_pdu_endpointConfigVersion.cmake
      ${CMAKE_CURRENT_BINARY_DIR}/HakoniwaPduEndpointConfig.cmake
      ${CMAKE_CURRENT_BINARY_DIR}/HakoniwaPduEndpointConfigVersion.cmake
    DESTINATION ${hakoniwa_pdu_endpoint_cmake_dir}
  )
endif()
