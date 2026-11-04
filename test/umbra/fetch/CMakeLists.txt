add_executable(umbra_fetch_unittests
  content_steering_test.cc
  init_cache_test.cc
  recovery_ladder_test.cc
  recovery_primitives_test.cc
  segment_fetcher_test.cc)
target_link_libraries(umbra_fetch_unittests PRIVATE
  umbra::fetch alumbra_test::fake_port umbra::base_test_support
  GTest::gtest_main)
target_include_directories(umbra_fetch_unittests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
alumbra_target_options(umbra_fetch_unittests)
alumbra_disable_exceptions(umbra_fetch_unittests)
gtest_discover_tests(umbra_fetch_unittests
  TEST_PREFIX "umbra_fetch_unittests."
  DISCOVERY_TIMEOUT 60)
