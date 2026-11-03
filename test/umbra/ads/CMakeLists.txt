add_executable(umbra_ads_unittests vast_parser_test.cc)
target_link_libraries(umbra_ads_unittests PRIVATE umbra::ads GTest::gtest_main)
target_compile_definitions(umbra_ads_unittests PRIVATE
  ALUMBRA_VAST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
alumbra_target_options(umbra_ads_unittests)
target_compile_options(umbra_ads_unittests PRIVATE -fno-exceptions)
gtest_discover_tests(umbra_ads_unittests
  TEST_PREFIX "umbra_ads_unittests."
  DISCOVERY_TIMEOUT 60)
