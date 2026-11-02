find_package(absl CONFIG REQUIRED)

add_executable(penumbra_interface_test
  rational_test.cc
  media_time_test.cc
  shared_bytes_test.cc
  encryption_info_test.cc
  encoded_sample_test.cc
  stream_info_test.cc)
target_link_libraries(penumbra_interface_test PRIVATE
  penumbra::interface
  absl::int128
  absl::str_format
  absl::strings
  GTest::gtest_main)
alumbra_target_options(penumbra_interface_test)
gtest_discover_tests(penumbra_interface_test
  TEST_PREFIX "penumbra_interface_test.")

add_executable(penumbra_port_test fake_port.cc port_test.cc)
target_link_libraries(penumbra_port_test PRIVATE
  penumbra::conformance_support penumbra::interface umbra::base_test_support GTest::gtest_main)
alumbra_target_options(penumbra_port_test)
gtest_discover_tests(penumbra_port_test TEST_PREFIX "penumbra_port_test.")

add_executable(penumbra_fake_port_conformance_test
  fake_port.cc fake_harness.cc fake_port_conformance_test.cc)
target_link_libraries(penumbra_fake_port_conformance_test PRIVATE
  penumbra::conformance_kit penumbra::conformance_main
  penumbra::interface umbra::base_test_support GTest::gtest)
alumbra_target_options(penumbra_fake_port_conformance_test)
gtest_discover_tests(penumbra_fake_port_conformance_test
  TEST_PREFIX "penumbra_conformance.")
add_test(NAME penumbra_conformance_report
  COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/test/penumbra_conformance/check_report.py"
    --executable "$<TARGET_FILE:penumbra_fake_port_conformance_test>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/fake_port_conformance_report.json"
    --port fake)

add_executable(penumbra_broken_port_conformance_test
  fake_port.cc fake_harness.cc broken_port_conformance_test.cc)
target_link_libraries(penumbra_broken_port_conformance_test PRIVATE
  penumbra::conformance_kit penumbra::conformance_main
  penumbra::interface umbra::base_test_support GTest::gtest)
alumbra_target_options(penumbra_broken_port_conformance_test)
set(_broken_port_expectations
  "broken_reentrant_callback|CallbackThreadingConformance|EventsAreDeliveredInsideACallbackScope"
  "broken_ignored_seek_ticket|SeekTicketConformance|SeekRequiresAStrictlyIncreasingTicket"
  "broken_ignored_seek_ticket|SeekTicketConformance|StaleWriteIsDiscardedWithoutConsumingTheGrant"
  "broken_non_monotonic_audio_position|ClockConformance|MediaTimeNeverDecreasesWhilePlaying")
foreach(_expectation IN LISTS _broken_port_expectations)
  string(REPLACE "|" ";" _fields "${_expectation}")
  list(GET _fields 0 _port)
  list(GET _fields 1 _suite)
  list(GET _fields 2 _case)
  set(_name "penumbra_broken_port.${_port}.${_suite}.${_case}")
  add_test(NAME "${_name}"
    COMMAND penumbra_broken_port_conformance_test
      "--gtest_filter=Conformance/${_suite}.${_case}/${_port}")
  set_tests_properties("${_name}" PROPERTIES WILL_FAIL TRUE)
endforeach()

file(GLOB _internal_headers CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/penumbra/interface/internal_include/penumbra/interface/*.h")
set(_header_sources "")
foreach(_header IN LISTS _internal_headers)
  get_filename_component(_name "${_header}" NAME)
  set(_source "${CMAKE_CURRENT_BINARY_DIR}/headers/${_name}.cc")
  file(GENERATE OUTPUT "${_source}" CONTENT
    "#include \"penumbra/interface/${_name}\"\nstatic_assert(__cplusplus == 202002L);\n")
  list(APPEND _header_sources "${_source}")
endforeach()
add_library(penumbra_port_header_compile OBJECT ${_header_sources})
target_link_libraries(penumbra_port_header_compile PRIVATE penumbra::interface)
alumbra_target_options(penumbra_port_header_compile)
set_target_properties(penumbra_port_header_compile PROPERTIES
  CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
