add_executable(alumbra_cli_args_test
  alumbra_cli_args_test.cc
  "${PROJECT_SOURCE_DIR}/antumbra/apps/alumbra-cli/args.cc")
target_include_directories(alumbra_cli_args_test PRIVATE
  "${PROJECT_SOURCE_DIR}/antumbra/apps/alumbra-cli/include")
target_compile_definitions(alumbra_cli_args_test PRIVATE
  ALUMBRA_CLI_SETTINGS_INDEX="${PROJECT_SOURCE_DIR}/docs/reference/settings/index.json")
target_link_libraries(alumbra_cli_args_test PRIVATE
  GTest::gtest_main)
alumbra_target_options(alumbra_cli_args_test)
gtest_discover_tests(alumbra_cli_args_test
  TEST_PREFIX "alumbra_cli_args_test.")

foreach(_case IN ITEMS mp4 ts webm)
  if(_case STREQUAL "mp4")
    set(_media "${PROJECT_SOURCE_DIR}/test/umbra/demux/fixtures/progressive_faststart.mp4")
  elseif(_case STREQUAL "ts")
    set(_media "${PROJECT_SOURCE_DIR}/test/umbra/demux/fixtures/h264_aac.ts")
  else()
    set(_media "${PROJECT_SOURCE_DIR}/test/umbra/demux/fixtures/webm_vp8_vorbis.webm")
  endif()
  add_test(NAME alumbra_cli_headless_${_case}
    COMMAND $<TARGET_FILE:alumbra-cli> --headless "${_media}")
  set_tests_properties(alumbra_cli_headless_${_case} PROPERTIES
    LABELS "cli_integration")
endforeach()

add_test(NAME alumbra_cli_headless_http
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/cli_http_integration_test.py"
    "$<TARGET_FILE:alumbra-cli>"
    "${PROJECT_SOURCE_DIR}/test/umbra/demux/fixtures")
set_tests_properties(alumbra_cli_headless_http PROPERTIES
  LABELS "cli_integration")

add_test(NAME alumbra_cli_unsupported_scheme_exit
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/cli_exit_test.py"
    "$<TARGET_FILE:alumbra-cli>" "custom-scheme://media" "14")
set_tests_properties(alumbra_cli_unsupported_scheme_exit PROPERTIES
  LABELS "cli_integration")
