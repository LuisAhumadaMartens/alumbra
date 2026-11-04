add_executable(umbra_source_unittests
  buffered_reader_test.cc
  byte_stream_data_source_test.cc
  fake_data_source.cc
  fake_http_transport.cc
  fake_stream_provider.cc
  file_url_test.cc
  http_byte_stream_provider_test.cc
  source_resolver_test.cc
  source_type_detector_test.cc
  source_type_test.cc
  url_test.cc)
target_link_libraries(umbra_source_unittests PRIVATE
  umbra::source umbra::base_test_support GTest::gtest_main)
target_compile_definitions(umbra_source_unittests PRIVATE
  ALUMBRA_SOURCE_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/test/umbra/demux/fixtures")
alumbra_target_options(umbra_source_unittests)
alumbra_disable_exceptions(umbra_source_unittests)
gtest_discover_tests(umbra_source_unittests
  TEST_PREFIX "umbra_source_unittests."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300)
