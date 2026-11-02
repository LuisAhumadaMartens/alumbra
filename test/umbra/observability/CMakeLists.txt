add_library(umbra_observability_benchmark_workload STATIC
  benchmark_workload.cc)
target_include_directories(umbra_observability_benchmark_workload PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(umbra_observability_benchmark_workload PUBLIC
  umbra::observability)
alumbra_target_options(umbra_observability_benchmark_workload)

add_executable(umbra_observability_unittests
  compiled_out_test.cc
  floor_test.cc
  log_test.cc
  port_sink_test.cc
  settings_binding_test.cc
  trace_test.cc)
target_include_directories(umbra_observability_unittests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(umbra_observability_unittests PRIVATE
  umbra::observability umbra::observability_settings
  umbra::base_test_support GTest::gtest_main Threads::Threads)
alumbra_target_options(umbra_observability_unittests)
target_compile_options(umbra_observability_unittests PRIVATE -fno-exceptions)
gtest_discover_tests(umbra_observability_unittests
  TEST_PREFIX "umbra_observability_unittests."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300)

add_executable(trace_disabled_benchmark trace_disabled_benchmark.cc)
target_link_libraries(trace_disabled_benchmark PRIVATE
  umbra_observability_benchmark_workload benchmark::benchmark)
alumbra_target_options(trace_disabled_benchmark)
add_test(NAME trace_disabled_benchmark
  COMMAND trace_disabled_benchmark --benchmark_min_time=0.01s)
set_tests_properties(trace_disabled_benchmark PROPERTIES LABELS benchmark)

add_executable(trace_enabled_benchmark trace_enabled_benchmark.cc)
target_link_libraries(trace_enabled_benchmark PRIVATE
  umbra_observability_benchmark_workload benchmark::benchmark)
alumbra_target_options(trace_enabled_benchmark)
add_test(NAME trace_enabled_benchmark
  COMMAND trace_enabled_benchmark --benchmark_min_time=0.01s)
set_tests_properties(trace_enabled_benchmark PROPERTIES LABELS benchmark)

add_executable(trace_strings_probe_off trace_strings_probe.cc)
target_include_directories(trace_strings_probe_off PRIVATE
  "${PROJECT_SOURCE_DIR}/umbra/observability/internal_include")
target_compile_definitions(trace_strings_probe_off PRIVATE
  ALUMBRA_TRACE_ENABLED=0)
alumbra_target_options(trace_strings_probe_off)

add_executable(trace_strings_probe_on trace_strings_probe.cc)
target_link_libraries(trace_strings_probe_on PRIVATE umbra::observability)
target_compile_definitions(trace_strings_probe_on PRIVATE
  ALUMBRA_TRACE_ENABLED=1)
alumbra_target_options(trace_strings_probe_on)

set(_binary_strings
  "${PROJECT_SOURCE_DIR}/tools/testing/check_binary_strings.py")
add_test(NAME trace_strings_checker_unit
  COMMAND "${Python3_EXECUTABLE}" -m unittest check_binary_strings_test
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/tools/testing")
add_test(NAME trace_strings_absent_when_compiled_out
  COMMAND "${Python3_EXECUTABLE}" "${_binary_strings}"
    "$<TARGET_FILE:trace_strings_probe_off>" --absent-categories
    --absent trace_probe_unique_scope
    --absent trace_probe_unique_instant
    --absent trace_probe_unique_counter)
add_test(NAME trace_strings_present_when_enabled
  COMMAND "${Python3_EXECUTABLE}" "${_binary_strings}"
    "$<TARGET_FILE:trace_strings_probe_on>"
    --present trace_probe_unique_scope
    --present trace_probe_unique_instant
    --present trace_probe_unique_counter)
