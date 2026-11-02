add_library(umbra_pipeline_test_support STATIC fake_sample_source.cc)
add_library(alumbra_test::fake_sample_source ALIAS umbra_pipeline_test_support)
target_include_directories(umbra_pipeline_test_support PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(umbra_pipeline_test_support PUBLIC umbra::pipeline)
alumbra_target_options(umbra_pipeline_test_support)
target_compile_options(umbra_pipeline_test_support PRIVATE -fno-exceptions)

add_executable(umbra_pipeline_unittests
  event_queue_test.cc
  playback_state_machine_test.cc
  playback_state_test.cc)
target_link_libraries(umbra_pipeline_unittests PRIVATE
  umbra::pipeline alumbra_test::fake_sample_source alumbra_test::fake_port
  GTest::gtest_main)
alumbra_target_options(umbra_pipeline_unittests)
target_compile_options(umbra_pipeline_unittests PRIVATE -fno-exceptions)
gtest_discover_tests(umbra_pipeline_unittests
  TEST_PREFIX "umbra_pipeline_unittests."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300)
