add_executable(umbra_base_unittests
  bit_reader_test.cc
  byte_reader_test.cc
  executor_harness.cc
  sequence_checker_test.cc
  status_test.cc
  task_runner_test.cc
  test_task_environment_test.cc
  thread_pool_test.cc
  url_redactor_test.cc
  weak_ptr_test.cc)
target_link_libraries(umbra_base_unittests PRIVATE
  umbra::base_test_support GTest::gtest_main)
alumbra_target_options(umbra_base_unittests)
target_compile_options(umbra_base_unittests PRIVATE -fno-exceptions)
gtest_discover_tests(umbra_base_unittests
  TEST_PREFIX "umbra_base_unittests."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300)

add_executable(task_runner_benchmark task_runner_benchmark.cc)
target_link_libraries(task_runner_benchmark PRIVATE
  umbra::base_test_support benchmark::benchmark_main)
alumbra_target_options(task_runner_benchmark)
add_test(NAME task_runner_benchmark
  COMMAND task_runner_benchmark --benchmark_min_time=0.01s)
set_tests_properties(task_runner_benchmark PROPERTIES LABELS benchmark)
