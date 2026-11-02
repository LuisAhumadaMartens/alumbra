add_executable(umbra_source_unittests
  buffered_reader_test.cc
  fake_data_source.cc
  source_resolver_test.cc
  source_type_detector_test.cc
  source_type_test.cc
  url_test.cc)
target_link_libraries(umbra_source_unittests PRIVATE
  umbra::source umbra::base_test_support GTest::gtest_main)
alumbra_target_options(umbra_source_unittests)
target_compile_options(umbra_source_unittests PRIVATE -fno-exceptions)
gtest_discover_tests(umbra_source_unittests
  TEST_PREFIX "umbra_source_unittests."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300)
