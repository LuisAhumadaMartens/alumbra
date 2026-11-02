add_executable(umbra_manifest_unittests
  ids_test.cc
  dash_mpd_parser_test.cc
  presentation_test.cc
  segment_index_test.cc)
target_link_libraries(umbra_manifest_unittests PRIVATE
  umbra::manifest GTest::gtest_main)
target_include_directories(umbra_manifest_unittests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(umbra_manifest_unittests PRIVATE
  DASH_MPD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
alumbra_target_options(umbra_manifest_unittests)
target_compile_options(umbra_manifest_unittests PRIVATE -fno-exceptions)
gtest_discover_tests(umbra_manifest_unittests
  TEST_PREFIX "umbra_manifest_unittests."
  DISCOVERY_TIMEOUT 60)
