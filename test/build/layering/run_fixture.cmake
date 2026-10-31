if(LAYERING_CHECK STREQUAL "links")
  set(_build "${LAYERING_WORK_DIR}/build")
  file(REMOVE_RECURSE "${_build}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      -S "${LAYERING_FIXTURE}" -B "${_build}"
      -G "${LAYERING_GENERATOR}"
      -C "${LAYERING_CACHE}"
      "-DALUMBRA_SOURCE_DIR=${LAYERING_SOURCE_DIR}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _output)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Configuring ${LAYERING_FIXTURE} failed:\n${_output}")
  endif()
  set(_arguments links --graph "${_build}/link-graph.tsv")
else()
  set(_arguments includes --root "${LAYERING_FIXTURE}")
endif()

execute_process(
  COMMAND "${LAYERING_PYTHON}"
    "${LAYERING_SOURCE_DIR}/tools/layering/check_layering.py" ${_arguments}
  RESULT_VARIABLE _result
  OUTPUT_VARIABLE _output
  ERROR_VARIABLE _output)
message("${_output}")

if(LAYERING_EXPECT STREQUAL "pass")
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Expected ${LAYERING_FIXTURE} to pass.")
  endif()
elseif(_result EQUAL 0)
  message(FATAL_ERROR "Expected ${LAYERING_FIXTURE} to fail.")
elseif(NOT _output MATCHES "${LAYERING_EXPECT_REGEX}")
  message(FATAL_ERROR
    "${LAYERING_FIXTURE} failed without the expected message: "
    "${LAYERING_EXPECT_REGEX}")
endif()
