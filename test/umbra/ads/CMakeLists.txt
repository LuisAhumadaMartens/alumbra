add_executable(umbra_ads_unittests obstruction_registry_test.cc)
target_link_libraries(umbra_ads_unittests PRIVATE
  umbra::ads GTest::gtest_main)
alumbra_target_options(umbra_ads_unittests)
target_compile_options(umbra_ads_unittests PRIVATE -fno-exceptions)
gtest_discover_tests(umbra_ads_unittests
  TEST_PREFIX "umbra_ads_unittests."
  DISCOVERY_TIMEOUT 60)
