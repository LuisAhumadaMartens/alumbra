add_executable(umbra_text_unittests caption_layout_test.cc layout_test.cc)
target_link_libraries(umbra_text_unittests PRIVATE
  umbra::text GTest::gtest_main)
alumbra_target_options(umbra_text_unittests)
gtest_discover_tests(umbra_text_unittests
  TEST_PREFIX "umbra_text_unittests.")
