add_executable(umbra_manifest_unittests
  ids_test.cc
  presentation_test.cc
  segment_index_test.cc)
target_link_libraries(umbra_manifest_unittests PRIVATE
  umbra::manifest GTest::gtest_main)
target_include_directories(umbra_manifest_unittests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
alumbra_target_options(umbra_manifest_unittests)
target_compile_options(umbra_manifest_unittests PRIVATE -fno-exceptions)
gtest_discover_tests(umbra_manifest_unittests
  TEST_PREFIX "umbra_manifest_unittests."
  DISCOVERY_TIMEOUT 60)
