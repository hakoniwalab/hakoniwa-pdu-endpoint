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

# add_subdirectory を使って src と test ディレクトリの CMakeLists.txt を含める
add_subdirectory(src)
if(HAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE)
  add_subdirectory(src/core_variants)
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

if(HAKO_PDU_ENDPOINT_INSTALL)
  install(
    TARGETS hakoniwa_pdu_endpoint
    EXPORT hakoniwa_pdu_endpointTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

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
