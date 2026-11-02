add_executable(penumbra_reference_player_test
  decoder_chain_test.cc
  decoder_selection_test.cc
  drop_attribution_test.cc
  scripted_decoder.cc)
target_include_directories(penumbra_reference_player_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/test")
target_link_libraries(penumbra_reference_player_test PRIVATE
  penumbra::reference_player
  penumbra_fake_port
  umbra::base_test_support
  GTest::gtest_main)
alumbra_target_options(penumbra_reference_player_test)
gtest_discover_tests(penumbra_reference_player_test
  TEST_PREFIX "penumbra_reference_player_test.")
