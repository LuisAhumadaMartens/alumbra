set(ALUMBRA_LOG_FLOOR "" CACHE STRING
  "Build-time log floor: verbose, debug, info, warning, error or off. Empty selects verbose for Debug builds and info otherwise.")
set_property(CACHE ALUMBRA_LOG_FLOOR PROPERTY STRINGS
  "" verbose debug info warning error off)
option(ALUMBRA_TRACE "Compile trace instrumentation into the artifact" ON)

set(_alumbra_log_floor "${ALUMBRA_LOG_FLOOR}")
if(_alumbra_log_floor STREQUAL "")
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_alumbra_log_floor verbose)
  else()
    set(_alumbra_log_floor info)
  endif()
endif()
set(_alumbra_log_floors verbose debug info warning error off)
list(FIND _alumbra_log_floors "${_alumbra_log_floor}" _alumbra_log_floor_level)
if(_alumbra_log_floor_level EQUAL -1)
  message(FATAL_ERROR "Unsupported ALUMBRA_LOG_FLOOR: ${ALUMBRA_LOG_FLOOR}")
endif()
if(ALUMBRA_TRACE)
  set(_alumbra_trace_enabled 1)
else()
  set(_alumbra_trace_enabled 0)
endif()

add_library(umbra_observability STATIC
  category.cc
  log.cc
  metrics.cc
  port_sink.cc
  trace.cc)
add_library(umbra::observability ALIAS umbra_observability)
set_target_properties(umbra_observability PROPERTIES
  EXPORT_NAME "umbra::observability")
target_include_directories(umbra_observability PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/internal_include")
target_compile_definitions(umbra_observability PUBLIC
  ALUMBRA_LOG_FLOOR_LEVEL=${_alumbra_log_floor_level}
  ALUMBRA_TRACE_ENABLED=${_alumbra_trace_enabled})
target_link_libraries(umbra_observability
  PUBLIC umbra::base penumbra::interface Threads::Threads)
alumbra_target_options(umbra_observability)
target_compile_options(umbra_observability PRIVATE -fno-exceptions)

add_library(umbra_observability_settings STATIC settings_binding.cc)
add_library(umbra::observability_settings ALIAS umbra_observability_settings)
set_target_properties(umbra_observability_settings PROPERTIES
  EXPORT_NAME "umbra::observability_settings")
target_link_libraries(umbra_observability_settings
  PUBLIC umbra::observability umbra::settings)
alumbra_target_options(umbra_observability_settings)
target_compile_options(umbra_observability_settings PRIVATE -fno-exceptions)
