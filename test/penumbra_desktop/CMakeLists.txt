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
gtest_discover_tests(penumbra_desktop_golden_test
  TEST_PREFIX "penumbra_desktop_golden_test."
  PROPERTIES LABELS golden)

find_program(ALUMBRA_SHADERCROSS shadercross
  PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/sdl3-shadercross"
  NO_DEFAULT_PATH)
if(ALUMBRA_SHADERCROSS)
  add_test(NAME shader_outputs_match_sources
    COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/tools/shaders/generate.py" --check
      --shadercross "${ALUMBRA_SHADERCROSS}"
      --output-dir "${CMAKE_CURRENT_BINARY_DIR}/shaders-regenerated"
      --library-path "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/lib")
endif()
add_test(NAME shader_generator.unit
  COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tools/shaders/generate_test.py")
