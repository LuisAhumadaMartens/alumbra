add_executable(umbra_pipeline_unittests
  event_queue_test.cc
  fake_sample_source.cc
  playback_state_machine_test.cc
  playback_state_test.cc)
target_link_libraries(umbra_pipeline_unittests PRIVATE
  umbra::pipeline alumbra_test::fake_port GTest::gtest_main)
alumbra_target_options(umbra_pipeline_unittests)
target_compile_options(umbra_pipeline_unittests PRIVATE -fno-exceptions)
gtest_discover_tests(umbra_pipeline_unittests
  TEST_PREFIX "umbra_pipeline_unittests."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300)
