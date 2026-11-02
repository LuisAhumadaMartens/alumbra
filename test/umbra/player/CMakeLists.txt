add_library(umbra_player_test_support STATIC player_fixture.cc)
add_library(alumbra_test::player_fixture ALIAS umbra_player_test_support)
target_include_directories(umbra_player_test_support PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(umbra_player_test_support PUBLIC
  umbra::player umbra::pipeline alumbra_test::fake_sample_source
  alumbra_test::fake_port umbra::base_test_support GTest::gtest)
alumbra_target_options(umbra_player_test_support)
target_compile_options(umbra_player_test_support PRIVATE -fno-exceptions)

add_executable(umbra_player_unittests
  can_play_type_test.cc
  events_test.cc
  player_test.cc
  srt_parser_test.cc
  text_track_test.cc
  value_types_test.cc)
target_link_libraries(umbra_player_unittests PRIVATE
  alumbra_test::player_fixture GTest::gtest_main)
alumbra_target_options(umbra_player_unittests)
target_compile_options(umbra_player_unittests PRIVATE -fno-exceptions)
gtest_discover_tests(umbra_player_unittests
  TEST_PREFIX "umbra_player_unittests."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300)
