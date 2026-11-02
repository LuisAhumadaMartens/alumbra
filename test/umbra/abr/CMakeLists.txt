add_executable(umbra_abr_unittests
  bandwidth_estimator_test.cc
  policy_test.cc
  rendition_filter_test.cc)
target_link_libraries(umbra_abr_unittests PRIVATE
  umbra::abr umbra::base_test_support GTest::gtest_main)
alumbra_target_options(umbra_abr_unittests)
target_compile_options(umbra_abr_unittests PRIVATE -fno-exceptions)
gtest_discover_tests(umbra_abr_unittests
  TEST_PREFIX "umbra_abr_unittests."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300)
