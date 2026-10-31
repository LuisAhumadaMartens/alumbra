cmake_minimum_required(VERSION 3.28)

# Placeholder version; release coordinates and policy are settled in D-34.
project(Alumbra VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
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
option(ALUMBRA_BUILD_TESTS "Build Alumbra tests" ON)
option(ALUMBRA_WARNINGS_AS_ERRORS "Treat Alumbra warnings as errors" ${_alumbra_ci})
option(ALUMBRA_BUILD_UI "Build the opt-in headless UI module skeleton" OFF)
set(ALUMBRA_PORT desktop CACHE STRING "Selected Penumbra port")
set_property(CACHE ALUMBRA_PORT PROPERTY STRINGS desktop)
set(ALUMBRA_SANITIZER none CACHE STRING "Sanitizer: none, address (with UBSan), or thread")
set_property(CACHE ALUMBRA_SANITIZER PROPERTY STRINGS none address thread)

if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Darwin|Linux)$")
  message(FATAL_ERROR "Only macOS and Linux desktop builds are supported in M0.")
endif()
if(NOT ALUMBRA_PORT STREQUAL "desktop")
  message(FATAL_ERROR "Only ALUMBRA_PORT=desktop is supported in M0.")
endif()
if(NOT ALUMBRA_SANITIZER MATCHES "^(none|address|thread)$")
  message(FATAL_ERROR "Unsupported ALUMBRA_SANITIZER: ${ALUMBRA_SANITIZER}")
endif()

include(cmake/AlumbraTargetOptions.cmake)
add_subdirectory(penumbra/interface)
add_subdirectory(penumbra/desktop)
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
  enable_testing()
  add_subdirectory(test)
endif()
