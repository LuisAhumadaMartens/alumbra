cmake_minimum_required(VERSION 3.28)

option(ALUMBRA_BUILD_TESTS "Build Alumbra tests" ON)
set(ALUMBRA_PORT desktop CACHE STRING
  "Selected Penumbra port: desktop, or none for the portable core only")
set_property(CACHE ALUMBRA_PORT PROPERTY STRINGS desktop none)
if(ALUMBRA_BUILD_TESTS)
  list(APPEND VCPKG_MANIFEST_FEATURES tests)
endif()
if(ALUMBRA_PORT STREQUAL "desktop")
  list(APPEND VCPKG_MANIFEST_FEATURES desktop)
endif()

project(Alumbra VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(_alumbra_ci OFF)
if(DEFINED ENV{CI} AND NOT "$ENV{CI}" STREQUAL ""
    AND NOT "$ENV{CI}" STREQUAL "false" AND NOT "$ENV{CI}" STREQUAL "0")
  set(_alumbra_ci ON)
endif()
option(ALUMBRA_WARNINGS_AS_ERRORS "Treat Alumbra warnings as errors" ${_alumbra_ci})
option(ALUMBRA_BUILD_UI "Build the opt-in headless UI module skeleton" OFF)
set(ALUMBRA_SANITIZER none CACHE STRING "Sanitizer: none, address (with UBSan), or thread")
set_property(CACHE ALUMBRA_SANITIZER PROPERTY STRINGS none address thread)
option(ALUMBRA_SANITIZER_CANARIES
  "Build deliberate sanitizer-failure canaries for CI validation" OFF)

if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Darwin|Linux)$")
  message(FATAL_ERROR "Only macOS and Linux desktop builds are supported in M0.")
endif()
if(NOT ALUMBRA_PORT MATCHES "^(desktop|none)$")
  message(FATAL_ERROR "Only ALUMBRA_PORT=desktop or none is supported in M0.")
endif()
if(NOT ALUMBRA_SANITIZER MATCHES "^(none|address|thread)$")
  message(FATAL_ERROR "Unsupported ALUMBRA_SANITIZER: ${ALUMBRA_SANITIZER}")
endif()
if(ALUMBRA_SANITIZER_CANARIES AND NOT ALUMBRA_BUILD_TESTS)
  message(FATAL_ERROR "ALUMBRA_SANITIZER_CANARIES requires ALUMBRA_BUILD_TESTS=ON.")
endif()
if(ALUMBRA_SANITIZER_CANARIES AND ALUMBRA_SANITIZER STREQUAL "none")
  message(FATAL_ERROR
    "ALUMBRA_SANITIZER_CANARIES requires ALUMBRA_SANITIZER=address or thread.")
endif()

include(cmake/AlumbraToolchain.cmake)
alumbra_check_toolchain()

find_package(absl CONFIG REQUIRED)
if(NOT DEFINED absl_VERSION OR absl_VERSION STREQUAL "")
  message(FATAL_ERROR
    "The Abseil package must report its version so Alumbra can export an exact ABI dependency.")
endif()
set(ALUMBRA_ABSL_VERSION "${absl_VERSION}")
find_package(Threads REQUIRED)

if(ALUMBRA_BUILD_TESTS)
  enable_testing()
  find_package(GTest CONFIG REQUIRED)
  find_package(benchmark CONFIG REQUIRED)
  include(GoogleTest)
endif()

include(cmake/AlumbraTargetOptions.cmake)
add_subdirectory(penumbra/interface)
if(ALUMBRA_PORT STREQUAL "desktop")
  add_subdirectory(penumbra/desktop)
endif()
add_subdirectory(umbra/base)
if(ALUMBRA_BUILD_UI)
  add_subdirectory(umbra/ui)
endif()

set(_alumbra_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/Alumbra")
configure_package_config_file(cmake/AlumbraConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/AlumbraConfig.cmake"
  INSTALL_DESTINATION "${_alumbra_package_dir}")
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/AlumbraConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY ExactVersion)
install(EXPORT AlumbraTargets FILE AlumbraTargets.cmake
  DESTINATION "${_alumbra_package_dir}")
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/AlumbraConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/AlumbraConfigVersion.cmake"
  DESTINATION "${_alumbra_package_dir}")

if(ALUMBRA_BUILD_TESTS)
  add_subdirectory(test)
endif()
