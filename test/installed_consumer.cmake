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
execute_process(COMMAND ${_configure} COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND ${_build} COMMAND_ERROR_IS_FATAL ANY)
execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${ALUMBRA_TEST_DIR}/build"
    --build-config "${ALUMBRA_CONFIG}" --output-on-failure --no-tests=error
  COMMAND_ERROR_IS_FATAL ANY)
