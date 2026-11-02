add_executable(umbra_codec_unittests
  audio_test.cc
  avc_test.cc
  codec_string_test.cc
  hevc_test.cc
  nal_unit_test.cc
  vp9_av1_test.cc)
target_link_libraries(umbra_codec_unittests PRIVATE
  umbra::codec GTest::gtest_main GTest::gmock)
target_include_directories(umbra_codec_unittests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")
alumbra_target_options(umbra_codec_unittests)
target_compile_options(umbra_codec_unittests PRIVATE -fno-exceptions)
gtest_discover_tests(umbra_codec_unittests
  TEST_PREFIX "umbra_codec_unittests."
  DISCOVERY_TIMEOUT 60)
