add_executable(design_tokens_test design_tokens_test.cc)
target_link_libraries(design_tokens_test PRIVATE
  alumbra::design_tokens GTest::gtest_main)
alumbra_target_options(design_tokens_test)
gtest_discover_tests(design_tokens_test
  TEST_PREFIX "design_tokens_test.")

add_executable(design_tokens_dump design_tokens_dump.cc)
target_link_libraries(design_tokens_dump PRIVATE alumbra::design_tokens)
alumbra_target_options(design_tokens_dump)
add_test(NAME design_tokens_golden
  COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tools/design/golden_tokens.py"
    --run "$<TARGET_FILE:design_tokens_dump>")
set_tests_properties(design_tokens_golden PROPERTIES LABELS design_tokens)
