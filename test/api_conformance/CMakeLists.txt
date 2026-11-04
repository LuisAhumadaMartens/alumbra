add_library(umbra_api_conformance STATIC
  element_harness.cc
  event_trace.cc)
add_library(alumbra_test::api_conformance ALIAS umbra_api_conformance)
target_include_directories(umbra_api_conformance PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(umbra_api_conformance PUBLIC
  alumbra_test::player_fixture
  absl::strings
  absl::str_format
  absl::time)
alumbra_target_options(umbra_api_conformance)
alumbra_disable_exceptions(umbra_api_conformance)

add_executable(umbra_api_conformance_tests
  event_trace_test.cc
  wpt_media_elements_test.cc)
target_compile_definitions(umbra_api_conformance_tests PRIVATE
  "ALUMBRA_TRACE_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/traces\"")
target_link_libraries(umbra_api_conformance_tests PRIVATE
  alumbra_test::api_conformance GTest::gtest_main)
alumbra_target_options(umbra_api_conformance_tests)
alumbra_disable_exceptions(umbra_api_conformance_tests)
gtest_discover_tests(umbra_api_conformance_tests
  TEST_PREFIX "umbra_api_conformance_tests."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300 LABELS "api_conformance;fake_port")
