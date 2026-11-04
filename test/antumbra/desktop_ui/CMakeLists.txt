find_package(ZLIB REQUIRED)

add_executable(desktop_ui_unit_test unit_test.cc)
target_include_directories(desktop_ui_unit_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/test/umbra/ui")
target_link_libraries(desktop_ui_unit_test PRIVATE
  alumbra::desktop_ui umbra::ui_test_support GTest::gtest_main)
alumbra_target_options(desktop_ui_unit_test)
gtest_discover_tests(desktop_ui_unit_test
  TEST_PREFIX "desktop_ui_unit_test."
  DISCOVERY_TIMEOUT 60)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/actual")
add_executable(desktop_ui_golden_test gpu_test.cc)
target_include_directories(desktop_ui_golden_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${PROJECT_SOURCE_DIR}/test/umbra/ui"
  "${PROJECT_SOURCE_DIR}/test/penumbra_desktop")
target_compile_definitions(desktop_ui_golden_test PRIVATE
  ALUMBRA_UI_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
  ALUMBRA_UI_ACTUAL_DIR="${CMAKE_CURRENT_BINARY_DIR}/actual")
target_link_libraries(desktop_ui_golden_test PRIVATE
  alumbra::desktop_ui umbra::ui_test_support GTest::gtest_main ZLIB::ZLIB)
alumbra_target_options(desktop_ui_golden_test)
set(_golden_properties LABELS golden)
if(ALUMBRA_SANITIZER STREQUAL "thread")
  list(APPEND _golden_properties ENVIRONMENT_MODIFICATION
    "TSAN_OPTIONS=string_append::suppressions=${PROJECT_SOURCE_DIR}/test/sanitizers/tsan.supp")
endif()
gtest_discover_tests(desktop_ui_golden_test
  TEST_PREFIX "desktop_ui_golden_test."
  DISCOVERY_TIMEOUT 60
  PROPERTIES ${_golden_properties})

add_test(NAME desktop_ui_literal_lint
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/ui/check_literals.py")
add_test(NAME desktop_ui_literal_lint.unit
  COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tools/ui/test_check_literals.py")
