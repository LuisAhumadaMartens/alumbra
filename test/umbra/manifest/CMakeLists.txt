add_executable(umbra_manifest_unittests
  ids_test.cc
  hls_refresh_test.cc
  hls_parser_test.cc
  presentation_test.cc
  segment_index_test.cc)
target_link_libraries(umbra_manifest_unittests PRIVATE
  umbra::manifest GTest::gtest_main)
target_include_directories(umbra_manifest_unittests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(umbra_manifest_unittests PRIVATE
  ALUMBRA_MANIFEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
alumbra_target_options(umbra_manifest_unittests)
target_compile_options(umbra_manifest_unittests PRIVATE -fno-exceptions)
gtest_discover_tests(umbra_manifest_unittests
  TEST_PREFIX "umbra_manifest_unittests."
  DISCOVERY_TIMEOUT 60)

add_executable(hls_parser_benchmark hls_parser_benchmark.cc)
target_link_libraries(hls_parser_benchmark PRIVATE
  umbra::manifest benchmark::benchmark_main)
alumbra_target_options(hls_parser_benchmark)
add_test(NAME hls_parser_benchmark
  COMMAND hls_parser_benchmark --benchmark_min_time=0.01s)
set_tests_properties(hls_parser_benchmark PROPERTIES LABELS benchmark)
