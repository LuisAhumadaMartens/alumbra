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
  CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_MAKE_PROGRAM)
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

add_test(NAME alumbra_installed_consumer
  COMMAND "${CMAKE_COMMAND}"
    "-DALUMBRA_BINARY_DIR=${PROJECT_BINARY_DIR}"
    "-DALUMBRA_CONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/consumer"
    "-DALUMBRA_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/installed-consumer"
    "-DALUMBRA_CONFIG=$<CONFIG>"
    "-DALUMBRA_GENERATOR=${CMAKE_GENERATOR}"
    "-DALUMBRA_CONSUMER_CACHE=${_consumer_cache_file}"
    "-DALUMBRA_BUILD_UI=${ALUMBRA_BUILD_UI}"
    "-DALUMBRA_PORT=${ALUMBRA_PORT}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/installed_consumer.cmake")
