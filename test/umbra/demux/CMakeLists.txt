add_executable(umbra_demux_unittests
  byte_queue_test.cc
  caption_extractor_test.cc
  ebml_test.cc
  mkv_block_test.cc
  mkv_builder.cc
  mkv_demuxer_test.cc
  mkv_golden_test.cc
  mp4_box_test.cc
  mp4_builder.cc
  mp4_demuxer_test.cc
  mp4_fragment_test.cc
  mp4_golden_test.cc
  mp4_source_test.cc
  packed_audio_demuxer_test.cc
  ../source/fake_data_source.cc
  test_support.cc)
target_link_libraries(umbra_demux_unittests PRIVATE
  umbra::demux umbra::source umbra::base_test_support GTest::gtest_main)
target_compile_definitions(umbra_demux_unittests PRIVATE
  ALUMBRA_DEMUX_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
alumbra_target_options(umbra_demux_unittests)
target_compile_options(umbra_demux_unittests PRIVATE -fno-exceptions
  "$<$<CXX_COMPILER_ID:GNU>:-Wno-array-bounds;-Wno-stringop-overflow;-Wno-free-nonheap-object>")
gtest_discover_tests(umbra_demux_unittests
  TEST_PREFIX "umbra_demux_unittests."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300)
