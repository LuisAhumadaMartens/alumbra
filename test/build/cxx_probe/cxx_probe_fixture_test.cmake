set(_binary_dir "${ALUMBRA_TEST_DIR}/${ALUMBRA_FIXTURE}")
set(_results "${_binary_dir}/cxx-probe-results.json")
file(REMOVE_RECURSE "${_binary_dir}")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${ALUMBRA_PROBE_SOURCE_DIR}" -B "${_binary_dir}"
    -G "${ALUMBRA_GENERATOR}"
    -C "${ALUMBRA_CONSUMER_CACHE}"
    "-DALUMBRA_CXX_FEATURES=${CMAKE_CURRENT_LIST_DIR}/${ALUMBRA_FIXTURE}.md"
    "-DALUMBRA_CXX_PROBES=${CMAKE_CURRENT_LIST_DIR}/probes"
  RESULT_VARIABLE _result
  OUTPUT_VARIABLE _output
  ERROR_VARIABLE _output)
message("${_output}")
if(NOT EXISTS "${_results}")
  message(FATAL_ERROR "The probe run wrote no result matrix.")
endif()
file(READ "${_results}" _json)

function(expect_feature probe status result)
  string(JSON _count LENGTH "${_json}" features)
  math(EXPR _last "${_count} - 1")
  foreach(_index RANGE ${_last})
    string(JSON _probe GET "${_json}" features ${_index} probe)
    if(_probe STREQUAL probe)
      string(JSON _status GET "${_json}" features ${_index} status)
      string(JSON _result GET "${_json}" features ${_index} result)
      if(NOT _status STREQUAL status OR NOT _result STREQUAL result)
        message(FATAL_ERROR
          "${probe}: expected ${status}/${result}, got ${_status}/${_result}")
      endif()
      return()
    endif()
  endforeach()
  message(FATAL_ERROR "${probe} is missing from the result matrix.")
endfunction()

if(ALUMBRA_FIXTURE STREQUAL "allowed_failure")
  if(_result EQUAL 0)
    message(FATAL_ERROR "Failing allowed features did not fail the probe run.")
  endif()
  foreach(_probe IN ITEMS fixture_compile_error fixture_run_failure)
    string(FIND "${_output}" "${_probe}" _at)
    if(_at EQUAL -1)
      message(FATAL_ERROR "The failure does not name ${_probe}.")
    endif()
  endforeach()
  expect_feature(fixture_supported allowed supported)
  expect_feature(fixture_compile_error allowed unsupported)
  expect_feature(fixture_run_failure allowed run-failed)
elseif(ALUMBRA_FIXTURE STREQUAL "informative")
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Unsupported pending or banned features failed the run.")
  endif()
  expect_feature(fixture_supported banned supported)
  expect_feature(fixture_compile_error pending unsupported)
  expect_feature(fixture_run_failure pending run-failed)
else()
  message(FATAL_ERROR "Unknown fixture ${ALUMBRA_FIXTURE}")
endif()
