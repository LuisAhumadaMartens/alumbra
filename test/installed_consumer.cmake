file(REMOVE_RECURSE "${ALUMBRA_TEST_DIR}")

set(_prefix "${ALUMBRA_TEST_DIR}/prefix")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${ALUMBRA_BINARY_DIR}"
    --prefix "${_prefix}" --config "${ALUMBRA_CONFIG}"
  COMMAND_ERROR_IS_FATAL ANY)
if(DEFINED ALUMBRA_EXTRA_HEADERS)
  file(COPY "${ALUMBRA_EXTRA_HEADERS}/"
    DESTINATION "${_prefix}/${ALUMBRA_INCLUDEDIR}")
endif()
set(_configure
  "${CMAKE_COMMAND}"
  -S "${ALUMBRA_CONSUMER_SOURCE_DIR}" -B "${ALUMBRA_TEST_DIR}/build"
  -G "${ALUMBRA_GENERATOR}"
  -C "${ALUMBRA_CONSUMER_CACHE}"
  "-DCMAKE_BUILD_TYPE=${ALUMBRA_CONFIG}"
  "-DCMAKE_PREFIX_PATH=${_prefix}"
  "-DALUMBRA_INSTALL_PREFIX=${_prefix}"
  "-DALUMBRA_EXPECT_UI=${ALUMBRA_BUILD_UI}"
  "-DALUMBRA_EXPECT_PORT=${ALUMBRA_PORT}"
  "-DALUMBRA_LAYERING_ALLOWLIST=${ALUMBRA_LAYERING_ALLOWLIST}"
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF
  -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF)
set(_build
  "${CMAKE_COMMAND}" --build "${ALUMBRA_TEST_DIR}/build"
  --config "${ALUMBRA_CONFIG}")
if(DEFINED ALUMBRA_EXPECT_ABSL_VERSION)
  set(_mismatched "${ALUMBRA_TEST_DIR}/mismatched-absl")
  file(MAKE_DIRECTORY "${_mismatched}")
  file(WRITE "${_mismatched}/abslConfig.cmake"
    "message(FATAL_ERROR \"Mismatched Abseil configuration was loaded\")\n")
  include(CMakePackageConfigHelpers)
  write_basic_package_version_file("${_mismatched}/abslConfigVersion.cmake"
    VERSION 0.0.0 COMPATIBILITY ExactVersion)
  list(APPEND _configure
    "-Dabsl_DIR=${_mismatched}"
    "-DCMAKE_IGNORE_PATH=${ALUMBRA_PRODUCER_ABSL_DIR}"
    -DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=OFF
    -DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=OFF
    -DCMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH=OFF)
  execute_process(COMMAND ${_configure}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _output)
  message("${_output}")
  if(_result EQUAL 0)
    message(FATAL_ERROR "The installed consumer accepted mismatched Abseil.")
  endif()
  string(REGEX REPLACE "[ \t\r\n]+" " " _normalized_output "${_output}")
  foreach(_expected IN ITEMS "absl" "${ALUMBRA_EXPECT_ABSL_VERSION}" "0.0.0"
      "exactly matches requested version" "Set absl_DIR" "rebuild Alumbra")
    string(FIND "${_normalized_output}" "${_expected}" _diagnostic_at)
    if(_diagnostic_at EQUAL -1)
      message(FATAL_ERROR
        "The consumer did not reject the Abseil version during package lookup: ${_expected}")
    endif()
  endforeach()
  if(EXISTS "${ALUMBRA_TEST_DIR}/build/build.ninja")
    message(FATAL_ERROR "The mismatched consumer reached build generation.")
  endif()
  return()
endif()
if(DEFINED ALUMBRA_EXPECT_HEADER_FAILURE)
  execute_process(COMMAND ${_configure}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _output)
  if(_result EQUAL 0)
    execute_process(COMMAND ${_build}
      RESULT_VARIABLE _result
      OUTPUT_VARIABLE _build_output
      ERROR_VARIABLE _build_output)
    string(APPEND _output "${_build_output}")
  endif()
  message("${_output}")
  if(_result EQUAL 0)
    message(FATAL_ERROR
      "The C++20 consumer accepted ${ALUMBRA_EXPECT_HEADER_FAILURE}.")
  endif()
  string(FIND "${_output}" "${ALUMBRA_EXPECT_HEADER_FAILURE}" _header_at)
  string(FIND "${_output}" "${ALUMBRA_EXPECT_DIAGNOSTIC}" _diagnostic_at)
  if(_header_at EQUAL -1 OR _diagnostic_at EQUAL -1)
    message(FATAL_ERROR
      "The C++20 consumer failed, but not on "
      "${ALUMBRA_EXPECT_HEADER_FAILURE}: ${ALUMBRA_EXPECT_DIAGNOSTIC}.")
  endif()
  return()
endif()
if(DEFINED ALUMBRA_REFERENCE_DOXYFILE)
  set(_header_globs "*.h" "*.hpp" "*.hh" "*.hxx")
  file(STRINGS "${ALUMBRA_REFERENCE_DOXYFILE}" _reference_inputs
    REGEX "^INPUT[ \t]*=")
  string(REGEX REPLACE "^INPUT[ \t]*=[ \t]*" "" _reference_inputs
    "${_reference_inputs}")
  separate_arguments(_reference_inputs UNIX_COMMAND "${_reference_inputs}")
  set(_reference_headers "")
  foreach(_input IN LISTS _reference_inputs)
    set(_input_dir "${ALUMBRA_SOURCE_DIR}/${_input}")
    list(TRANSFORM _header_globs PREPEND "${_input_dir}/" OUTPUT_VARIABLE _globs)
    file(GLOB_RECURSE _headers RELATIVE "${_input_dir}" ${_globs})
    list(APPEND _reference_headers ${_headers})
  endforeach()
  set(_installed_dir "${_prefix}/${ALUMBRA_INCLUDEDIR}")
  list(TRANSFORM _header_globs PREPEND "${_installed_dir}/" OUTPUT_VARIABLE _globs)
  file(GLOB_RECURSE _installed_headers RELATIVE "${_installed_dir}" ${_globs})
  list(SORT _reference_headers)
  list(SORT _installed_headers)
  if(DEFINED ALUMBRA_EXPECT_REFERENCE_MISMATCH)
    if(NOT ALUMBRA_EXPECT_REFERENCE_MISMATCH IN_LIST _installed_headers
        OR ALUMBRA_EXPECT_REFERENCE_MISMATCH IN_LIST _reference_headers)
      message(FATAL_ERROR "The C++ API reference input check accepted "
        "${ALUMBRA_EXPECT_REFERENCE_MISMATCH}.")
    endif()
    return()
  endif()
  if(NOT _reference_headers STREQUAL _installed_headers)
    list(JOIN _installed_headers "\n    " _installed_list)
    list(JOIN _reference_headers "\n    " _reference_list)
    message(FATAL_ERROR
      "The C++ API reference must read exactly the installed public headers. "
      "Update INPUT in ${ALUMBRA_REFERENCE_DOXYFILE}.\n"
      "  Installed:\n    ${_installed_list}\n"
      "  Reference inputs:\n    ${_reference_list}")
  endif()
endif()
execute_process(COMMAND ${_configure} COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND ${_build} COMMAND_ERROR_IS_FATAL ANY)
execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${ALUMBRA_TEST_DIR}/build"
    --build-config "${ALUMBRA_CONFIG}" --output-on-failure --no-tests=error
  COMMAND_ERROR_IS_FATAL ANY)
