foreach(_wrong IN ITEMS OFF ON)
  set(_build "${ALUMBRA_TEST_DIR}/${_wrong}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --fresh -G "${ALUMBRA_GENERATOR}"
      -C "${ALUMBRA_CONSUMER_CACHE}"
      "-DALUMBRA_SOURCE=${ALUMBRA_SOURCE}" "-DALUMBRA_WRONG_TYPE=${_wrong}"
      -S "${ALUMBRA_SOURCE}/test/build/settings_typed_get" -B "${_build}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Typed-get fixture configure failed: ${_output}${_error}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_build}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
  if(_wrong)
    if(_result EQUAL 0 OR NOT "${_output}${_error}" MATCHES
        "no matching (member )?function|could not match|cannot convert")
      message(FATAL_ERROR
        "Expected Get<bool> on an integer key to fail to compile: ${_output}${_error}")
    endif()
  elseif(NOT _result EQUAL 0)
    message(FATAL_ERROR "Matching-type control failed: ${_output}${_error}")
  endif()
endforeach()
