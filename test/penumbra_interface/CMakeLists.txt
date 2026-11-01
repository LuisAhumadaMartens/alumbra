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
