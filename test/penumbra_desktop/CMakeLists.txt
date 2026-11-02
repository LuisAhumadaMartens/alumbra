add_executable(penumbra_desktop_video_test video_conversion_test.cc)
target_link_libraries(penumbra_desktop_video_test PRIVATE
  penumbra::desktop GTest::gtest_main)
alumbra_target_options(penumbra_desktop_video_test)
gtest_discover_tests(penumbra_desktop_video_test
  TEST_PREFIX "penumbra_desktop_video_test.")

add_executable(penumbra_desktop_golden_test video_golden_test.cc)
target_link_libraries(penumbra_desktop_golden_test PRIVATE
  penumbra::desktop GTest::gtest_main)
alumbra_target_options(penumbra_desktop_golden_test)
set(_golden_properties LABELS golden)
if(ALUMBRA_SANITIZER STREQUAL "thread")
  list(APPEND _golden_properties ENVIRONMENT_MODIFICATION
    "TSAN_OPTIONS=string_append::suppressions=${PROJECT_SOURCE_DIR}/test/sanitizers/tsan.supp")
endif()
gtest_discover_tests(penumbra_desktop_golden_test
  TEST_PREFIX "penumbra_desktop_golden_test."
  PROPERTIES ${_golden_properties})

set(_shader_host_root "${VCPKG_INSTALLED_DIR}/x64-linux")
find_program(ALUMBRA_SHADERCROSS shadercross
  PATHS "${_shader_host_root}/tools/sdl3-shadercross"
  NO_DEFAULT_PATH)
if(ALUMBRA_SHADERCROSS)
  add_test(NAME shader_outputs_match_sources
    COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/shaders/generate.py" --check
      --shadercross "${ALUMBRA_SHADERCROSS}"
      --output-dir "${CMAKE_CURRENT_BINARY_DIR}/shaders-regenerated"
      --library-path "${_shader_host_root}/lib")
endif()
add_test(NAME shader_generator.unit
  COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tools/shaders/generate_test.py")
