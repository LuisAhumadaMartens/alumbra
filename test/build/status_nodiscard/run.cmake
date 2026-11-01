foreach(_ignore IN ITEMS OFF ON)
  set(_build "${ALUMBRA_TEST_DIR}/${_ignore}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --fresh -G "${ALUMBRA_GENERATOR}"
      -C "${ALUMBRA_CONSUMER_CACHE}"
      "-DALUMBRA_SOURCE=${ALUMBRA_SOURCE}" "-DALUMBRA_IGNORE_STATUS=${_ignore}"
      -S "${ALUMBRA_SOURCE}/test/build/status_nodiscard" -B "${_build}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Status fixture configure failed: ${_output}${_error}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_build}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
  if(_ignore)
    if(_result EQUAL 0 OR NOT "${_output}${_error}" MATCHES
        "ignoring return value|nodiscard")
      message(FATAL_ERROR "Expected unused-result diagnostic: ${_output}${_error}")
    endif()
  elseif(NOT _result EQUAL 0)
    message(FATAL_ERROR "Used-status fixture failed: ${_output}${_error}")
  endif()
endforeach()
