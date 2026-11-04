add_library(umbra_pipeline_test_support STATIC fake_sample_source.cc)
add_library(alumbra_test::fake_sample_source ALIAS umbra_pipeline_test_support)
target_include_directories(umbra_pipeline_test_support PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(umbra_pipeline_test_support PUBLIC umbra::pipeline)
alumbra_target_options(umbra_pipeline_test_support)
alumbra_disable_exceptions(umbra_pipeline_test_support)

add_executable(umbra_pipeline_unittests
  buffer_manager_test.cc
  adaptive_source_test.cc
  memory_profile_test.cc
  event_queue_test.cc
  playback_state_machine_test.cc
  playback_state_test.cc
  progressive_source_test.cc
  "${PROJECT_SOURCE_DIR}/test/umbra/source/fake_data_source.cc")
target_include_directories(umbra_pipeline_unittests PRIVATE
  "${PROJECT_SOURCE_DIR}/test/umbra/source")
target_link_libraries(umbra_pipeline_unittests PRIVATE
  umbra::pipeline alumbra_test::fake_sample_source alumbra_test::fake_port
  GTest::gtest_main)
alumbra_target_options(umbra_pipeline_unittests)
alumbra_disable_exceptions(umbra_pipeline_unittests)
target_compile_definitions(umbra_pipeline_unittests PRIVATE
  ALUMBRA_ADAPTIVE_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/test/umbra/demux/fixtures"
  ALUMBRA_PROGRESSIVE_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/test/umbra/demux/fixtures")
gtest_discover_tests(umbra_pipeline_unittests
  TEST_PREFIX "umbra_pipeline_unittests."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300)
