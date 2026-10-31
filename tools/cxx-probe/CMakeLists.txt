cmake_minimum_required(VERSION 3.28)
project(AlumbraCxxProbe LANGUAGES CXX)

set(ALUMBRA_CXX_FEATURES "${CMAKE_CURRENT_LIST_DIR}/../../docs/cpp-features.md"
  CACHE FILEPATH "Feature policy whose Probed features table drives the run")
set(ALUMBRA_CXX_PROBES "${CMAKE_CURRENT_LIST_DIR}/probes"
  CACHE PATH "Directory with one <probe>.cc per probed feature")
set(ALUMBRA_CXX_PROBE_RESULTS
  "${CMAKE_CURRENT_BINARY_DIR}/cxx-probe-results.json"
  CACHE FILEPATH "Machine-readable probe result matrix")

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/AlumbraToolchain.cmake")
alumbra_detect_toolchain(_toolchain)

file(READ "${ALUMBRA_CXX_FEATURES}" _document)
string(FIND "${_document}" "\n## Probed features\n" _start)
if(_start EQUAL -1)
  message(FATAL_ERROR
    "${ALUMBRA_CXX_FEATURES} has no \"## Probed features\" section.")
endif()
math(EXPR _start "${_start} + 1")
string(SUBSTRING "${_document}" ${_start} -1 _section)
string(FIND "${_section}" "\n## " _end)
if(NOT _end EQUAL -1)
  string(SUBSTRING "${_section}" 0 ${_end} _section)
endif()
string(REGEX MATCHALL "\n\\|[^\n|]+\\| `[a-z0-9_]+` \\| [a-z]+ \\|"
  _rows "${_section}")

set(_probes "")
foreach(_row IN LISTS _rows)
  string(REGEX MATCH "\\| *([^|]*[^| ]) *\\| `([a-z0-9_]+)` \\| ([a-z]+) \\|"
    _match "${_row}")
  set(_probe "${CMAKE_MATCH_2}")
  string(REPLACE "`" "" _feature_${_probe} "${CMAKE_MATCH_1}")
  set(_status_${_probe} "${CMAKE_MATCH_3}")
  if(NOT _status_${_probe} MATCHES "^(allowed|pending|banned)$")
    message(FATAL_ERROR
      "${_probe}: status must be allowed, pending or banned, not "
      "'${_status_${_probe}}'.")
  endif()
  if(_probe IN_LIST _probes)
    message(FATAL_ERROR "${_probe} is listed more than once.")
  endif()
  if(NOT EXISTS "${ALUMBRA_CXX_PROBES}/${_probe}.cc")
    message(FATAL_ERROR "${_probe} has no ${ALUMBRA_CXX_PROBES}/${_probe}.cc.")
  endif()
  list(APPEND _probes "${_probe}")
endforeach()
if(NOT _probes)
  message(FATAL_ERROR "${ALUMBRA_CXX_FEATURES} lists no probed features.")
endif()
file(GLOB _sources RELATIVE "${ALUMBRA_CXX_PROBES}" "${ALUMBRA_CXX_PROBES}/*.cc")
foreach(_source IN LISTS _sources)
  string(REGEX REPLACE "\\.cc$" "" _probe "${_source}")
  if(NOT _probe IN_LIST _probes)
    message(FATAL_ERROR
      "${_source} is not listed in ${ALUMBRA_CXX_FEATURES}.")
  endif()
endforeach()

set(_options "")
if(CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang|GNU)$")
  set(_options -Wall -Wextra -Wpedantic -Werror)
endif()
if(CMAKE_CROSSCOMPILING)
  set(_mode "compile-link")
else()
  set(_mode "run")
endif()

function(_alumbra_json_string out_var value)
  string(REPLACE "\\" "\\\\" value "${value}")
  string(REPLACE "\"" "\\\"" value "${value}")
  set(${out_var} "\"${value}\"" PARENT_SCOPE)
endfunction()

message(STATUS "cxx-probe toolchain: ${_toolchain_SUMMARY}")
message(STATUS "cxx-probe mode: ${_mode}")
set(_features "[]")
set(_index 0)
set(_allowed_failures "")
foreach(_probe IN LISTS _probes)
  set(_source "${ALUMBRA_CXX_PROBES}/${_probe}.cc")
  if(_mode STREQUAL "run")
    try_run(_run_${_probe} _compile_${_probe}
      SOURCES "${_source}"
      CXX_STANDARD 23
      CXX_STANDARD_REQUIRED ON
      CXX_EXTENSIONS OFF
      COMPILE_DEFINITIONS ${_options}
      COMPILE_OUTPUT_VARIABLE _compile_output
      RUN_OUTPUT_VARIABLE _run_output)
  else()
    try_compile(_compile_${_probe}
      SOURCES "${_source}"
      CXX_STANDARD 23
      CXX_STANDARD_REQUIRED ON
      CXX_EXTENSIONS OFF
      COMPILE_DEFINITIONS ${_options}
      OUTPUT_VARIABLE _compile_output)
  endif()
  if(NOT _compile_${_probe})
    set(_result "unsupported")
  elseif(_mode STREQUAL "run" AND NOT _run_${_probe} EQUAL 0)
    set(_result "run-failed")
  else()
    set(_result "supported")
  endif()
  set(_status "${_status_${_probe}}")
  message(STATUS "cxx-probe ${_status} ${_probe}: ${_result}")
  if(_status STREQUAL "allowed" AND NOT _result STREQUAL "supported")
    list(APPEND _allowed_failures "${_probe} (${_result})")
  endif()

  _alumbra_json_string(_probe_json "${_probe}")
  _alumbra_json_string(_feature_json "${_feature_${_probe}}")
  _alumbra_json_string(_status_json "${_status}")
  _alumbra_json_string(_result_json "${_result}")
  string(JSON _features SET "${_features}" ${_index}
    "{\"probe\":${_probe_json},\"feature\":${_feature_json},\"status\":${_status_json},\"result\":${_result_json}}")
  math(EXPR _index "${_index} + 1")
endforeach()

if(APPLE)
  set(_deployment "${CMAKE_OSX_DEPLOYMENT_TARGET}")
else()
  set(_deployment "${CMAKE_SYSTEM_VERSION}")
endif()
string(TIMESTAMP _generated "%Y-%m-%dT%H:%M:%SZ" UTC)
set(_json "{\"schema_version\":1}")
foreach(_entry IN ITEMS
    "generated|${_generated}"
    "mode|${_mode}"
    "compiler_id|${CMAKE_CXX_COMPILER_ID}"
    "compiler_version|${CMAKE_CXX_COMPILER_VERSION}"
    "apple_toolchain|${_toolchain_APPLE_TOOLCHAIN}"
    "apple_toolchain_version|${_toolchain_APPLE_TOOLCHAIN_VERSION}"
    "standard_library|${_toolchain_STDLIB}"
    "standard_library_version|${_toolchain_STDLIB_VERSION}"
    "cplusplus|${_toolchain_CPLUSPLUS}"
    "target_system|${CMAKE_SYSTEM_NAME}"
    "target_processor|${CMAKE_SYSTEM_PROCESSOR}"
    "target_architectures|${CMAKE_OSX_ARCHITECTURES}"
    "deployment_target|${_deployment}"
    "minimum_os_macro|${_toolchain_MIN_OS}")
  string(REGEX MATCH "^([^|]*)\\|(.*)$" _match "${_entry}")
  _alumbra_json_string(_value "${CMAKE_MATCH_2}")
  string(JSON _json SET "${_json}" "${CMAKE_MATCH_1}" "${_value}")
endforeach()
string(JSON _json SET "${_json}" features "${_features}")
file(WRITE "${ALUMBRA_CXX_PROBE_RESULTS}" "${_json}\n")
message(STATUS "cxx-probe results: ${ALUMBRA_CXX_PROBE_RESULTS}")

if(_allowed_failures)
  list(JOIN _allowed_failures ", " _failed)
  message(FATAL_ERROR
    "Allowed C++ features failed on ${_toolchain_SUMMARY}: ${_failed}. "
    "${ALUMBRA_CXX_FEATURES} marks them allowed, so every pinned toolchain "
    "must support them.")
endif()
