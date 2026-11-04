add_executable(umbra_text_unittests layout_test.cc style_test.cc ttml_parser_test.cc)
target_link_libraries(umbra_text_unittests PRIVATE
  umbra::text GTest::gtest_main)
alumbra_target_options(umbra_text_unittests)
gtest_discover_tests(umbra_text_unittests
  TEST_PREFIX "umbra_text_unittests.")
