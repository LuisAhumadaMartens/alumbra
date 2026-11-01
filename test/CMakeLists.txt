add_executable(umbra_base_smoke_test umbra_base_smoke_test.cc)
target_link_libraries(umbra_base_smoke_test PRIVATE
  umbra::base GTest::gtest_main)
alumbra_target_options(umbra_base_smoke_test)
gtest_discover_tests(umbra_base_smoke_test
  TEST_PREFIX "umbra_base_smoke_test.")

set(_manifest_check "${PROJECT_SOURCE_DIR}/cmake/check_vcpkg_manifest.cmake")
add_test(NAME vcpkg_manifest_pinning_test
  COMMAND "${CMAKE_COMMAND}" -P "${_manifest_check}")

function(alumbra_manifest_fixture name expected)
  add_test(NAME vcpkg_manifest_rejects_${name}
    COMMAND "${CMAKE_COMMAND}"
      "-DALUMBRA_VCPKG_MANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/${name}.json"
      -P "${_manifest_check}")
  set_tests_properties(vcpkg_manifest_rejects_${name} PROPERTIES
    PASS_REGULAR_EXPRESSION "${expected}")
endfunction()
alumbra_manifest_fixture(short_baseline
  "builtin-baseline must be a full 40-character commit SHA")
alumbra_manifest_fixture(override
  "overrides need a recorded rationale")
alumbra_manifest_fixture(unpinned_git_registry
  "registry baseline must be a full 40-character commit SHA")
alumbra_manifest_fixture(filesystem_registry
  "registry kind 'filesystem' is not allowed")
alumbra_manifest_fixture(escaping_overlay
  "overlay path '../triplets' must stay inside the repository")
alumbra_manifest_fixture(ffmpeg_default_features
  "ffmpeg must set default-features to false")
alumbra_manifest_fixture(ffmpeg_gpl
  "ffmpeg feature 'gpl' is not allowed")

foreach(_dir IN ITEMS CMAKE_INSTALL_LIBDIR CMAKE_INSTALL_INCLUDEDIR)
  cmake_path(SET _normalized NORMALIZE "${${_dir}}")
  if(IS_ABSOLUTE "${${_dir}}" OR _normalized MATCHES "^\\.\\.(/|$)")
    message(FATAL_ERROR
      "${_dir} must be a relative path inside the install prefix when "
      "ALUMBRA_BUILD_TESTS is ON because alumbra_installed_consumer installs "
      "into a private prefix.")
  endif()
endforeach()

set(_consumer_variables
  CMAKE_TOOLCHAIN_FILE VCPKG_CHAINLOAD_TOOLCHAIN_FILE VCPKG_TARGET_TRIPLET
  CMAKE_CXX_COMPILER CMAKE_CXX_COMPILER_TARGET CMAKE_SYSROOT
  CMAKE_OSX_ARCHITECTURES CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_OSX_SYSROOT
  CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_MAKE_PROGRAM absl_DIR)
foreach(_config IN LISTS CMAKE_CONFIGURATION_TYPES CMAKE_BUILD_TYPE)
  string(TOUPPER "${_config}" _config)
  list(APPEND _consumer_variables
    CMAKE_CXX_FLAGS_${_config} CMAKE_EXE_LINKER_FLAGS_${_config})
endforeach()
list(REMOVE_DUPLICATES _consumer_variables)

set(_consumer_cache "")
foreach(_variable IN LISTS _consumer_variables)
  if(DEFINED ${_variable})
    string(APPEND _consumer_cache
      "set(${_variable} [==[${${_variable}}]==] CACHE STRING \"\")\n")
  endif()
endforeach()
set(_consumer_cache_file
  "${CMAKE_CURRENT_BINARY_DIR}/installed-consumer-cache.cmake")
file(WRITE "${_consumer_cache_file}" "${_consumer_cache}")

set(_installed_consumer_command
  "${CMAKE_COMMAND}"
  "-DALUMBRA_BINARY_DIR=${PROJECT_BINARY_DIR}"
  "-DALUMBRA_CONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/consumer"
  "-DALUMBRA_CONFIG=$<CONFIG>"
  "-DALUMBRA_GENERATOR=${CMAKE_GENERATOR}"
  "-DALUMBRA_CONSUMER_CACHE=${_consumer_cache_file}"
  "-DALUMBRA_BUILD_UI=${ALUMBRA_BUILD_UI}"
  "-DALUMBRA_PORT=${ALUMBRA_PORT}")
add_test(NAME alumbra_installed_consumer
  COMMAND ${_installed_consumer_command}
    "-DALUMBRA_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/installed-consumer"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/installed_consumer.cmake")
add_test(NAME alumbra_installed_consumer_rejects_cxx23_header
  COMMAND ${_installed_consumer_command}
    "-DALUMBRA_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/installed-consumer-cxx23"
    "-DALUMBRA_EXTRA_HEADERS=${CMAKE_CURRENT_SOURCE_DIR}/build/public_header_cxx23"
    "-DALUMBRA_INCLUDEDIR=${CMAKE_INSTALL_INCLUDEDIR}"
    "-DALUMBRA_EXPECT_HEADER_FAILURE=umbra/fixture/cxx23_only.h"
    "-DALUMBRA_EXPECT_DIAGNOSTIC=Cxx23OnlyResult"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/installed_consumer.cmake")
set_tests_properties(
  alumbra_installed_consumer alumbra_installed_consumer_rejects_cxx23_header
  PROPERTIES RESOURCE_LOCK alumbra_install)

add_test(NAME alumbra_toolchain_floor_test
  COMMAND "${CMAKE_COMMAND}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/build/toolchain_floor/toolchain_floor_test.cmake")

set(_cxx_probe_arguments
  -G "${CMAKE_GENERATOR}"
  -C "${_consumer_cache_file}"
  "-DCMAKE_BUILD_TYPE=$<CONFIG>")
add_test(NAME cxx_probe
  COMMAND "${CMAKE_COMMAND}" --fresh ${_cxx_probe_arguments}
    -S "${PROJECT_SOURCE_DIR}/tools/cxx-probe"
    -B "${CMAKE_CURRENT_BINARY_DIR}/cxx-probe")
foreach(_fixture IN ITEMS allowed_failure informative)
  add_test(NAME cxx_probe_fixture_${_fixture}
    COMMAND "${CMAKE_COMMAND}"
      "-DALUMBRA_PROBE_SOURCE_DIR=${PROJECT_SOURCE_DIR}/tools/cxx-probe"
      "-DALUMBRA_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cxx-probe-fixtures"
      "-DALUMBRA_GENERATOR=${CMAKE_GENERATOR}"
      "-DALUMBRA_CONSUMER_CACHE=${_consumer_cache_file}"
      "-DALUMBRA_FIXTURE=${_fixture}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/build/cxx_probe/cxx_probe_fixture_test.cmake")
endforeach()
set_tests_properties(
  cxx_probe cxx_probe_fixture_allowed_failure cxx_probe_fixture_informative
  PROPERTIES LABELS cxx_probe)

add_subdirectory(build/layering)

add_subdirectory(umbra/base)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(_fake_clock_lint "${PROJECT_SOURCE_DIR}/tools/testing/check_fake_clock_sleeps.py")
add_test(NAME fake_clock_sleep_lint
  COMMAND "${Python3_EXECUTABLE}" "${_fake_clock_lint}"
    "${PROJECT_SOURCE_DIR}/test" "${PROJECT_SOURCE_DIR}/umbra"
    --exclude "${CMAKE_CURRENT_SOURCE_DIR}/build")
add_test(NAME fake_clock_sleep_lint.rejects_real_sleep
  COMMAND "${Python3_EXECUTABLE}" "${_fake_clock_lint}"
    "${CMAKE_CURRENT_SOURCE_DIR}/build/fake_clock_sleeps")
set_tests_properties(fake_clock_sleep_lint.rejects_real_sleep PROPERTIES
  PASS_REGULAR_EXPRESSION
    "sleeping_test\\.cc:8: real sleep in a test that uses the fake clock")
