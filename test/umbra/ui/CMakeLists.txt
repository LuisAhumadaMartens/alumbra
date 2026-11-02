add_library(umbra_ui_test_support STATIC harness.cc view_model_json.cc)
add_library(umbra::ui_test_support ALIAS umbra_ui_test_support)
set_target_properties(umbra_ui_test_support PROPERTIES
  EXPORT_NAME "umbra::ui_test_support")
target_include_directories(umbra_ui_test_support PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(umbra_ui_test_support PUBLIC
  umbra::ui umbra::base_test_support)
alumbra_target_options(umbra_ui_test_support)
target_compile_options(umbra_ui_test_support PRIVATE -fno-exceptions)

add_executable(umbra_ui_unittests
  behavior_test.cc
  fuzz_test.cc
  golden_test.cc)
target_link_libraries(umbra_ui_unittests PRIVATE
  umbra::ui_test_support GTest::gtest_main)
target_compile_definitions(umbra_ui_unittests PRIVATE
  ALUMBRA_UI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
alumbra_target_options(umbra_ui_unittests)
target_compile_options(umbra_ui_unittests PRIVATE -fno-exceptions)
gtest_discover_tests(umbra_ui_unittests
  TEST_PREFIX "umbra_ui_unittests."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300)

add_executable(ui_benchmark ui_benchmark.cc)
target_link_libraries(ui_benchmark PRIVATE
  umbra::ui_test_support benchmark::benchmark_main)
alumbra_target_options(ui_benchmark)
add_test(NAME ui_benchmark
  COMMAND ui_benchmark --benchmark_min_time=0.01s)
set_tests_properties(ui_benchmark PROPERTIES LABELS benchmark)
