find_package(SDL3 CONFIG REQUIRED)
include("${PROJECT_SOURCE_DIR}/cmake/AlumbraShaderBlobs.cmake")

set(ALUMBRA_SHADER_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/video/shaders")
set(ALUMBRA_GENERATED_SHADER_DIRECTORY "${ALUMBRA_SHADER_DIRECTORY}/generated")
set(_shader_blobs_source "${CMAKE_CURRENT_BINARY_DIR}/shader_blobs.cc")
alumbra_generate_shader_blobs("${_shader_blobs_source}"
  "${ALUMBRA_GENERATED_SHADER_DIRECTORY}"
  fullscreen_triangle.vert:FullscreenTriangleVertexShader
  yuv_to_rgb.frag:YuvToRgbFragmentShader)

add_library(penumbra_desktop STATIC
  desktop.cc
  video/color_conversion.cc
  video/gpu_presenter.cc
  video/plane_layout.cc
  video/sdl_session.cc
  video/video_geometry.cc
  video/video_sink.cc
  "${_shader_blobs_source}")
add_library(penumbra::desktop ALIAS penumbra_desktop)
set_target_properties(penumbra_desktop PROPERTIES EXPORT_NAME "penumbra::desktop")
target_include_directories(penumbra_desktop PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>")
target_link_libraries(penumbra_desktop
  PUBLIC
    penumbra::interface
    "$<BUILD_INTERFACE:SDL3::SDL3>"
    "$<BUILD_INTERFACE:absl::status>"
    "$<BUILD_INTERFACE:absl::statusor>"
  PRIVATE
    absl::strings)
alumbra_target_options(penumbra_desktop)
install(TARGETS penumbra_desktop EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

if(ALUMBRA_BUILD_TESTS)
  find_package(FFMPEG REQUIRED)
  add_executable(ffmpeg_license_test ffmpeg_license_test.cc)
  target_include_directories(ffmpeg_license_test SYSTEM PRIVATE
    ${FFMPEG_INCLUDE_DIRS})
  target_link_directories(ffmpeg_license_test PRIVATE ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(ffmpeg_license_test PRIVATE
    ${FFMPEG_LIBRARIES} ${CMAKE_DL_LIBS} GTest::gtest_main)
  alumbra_target_options(ffmpeg_license_test)
  gtest_discover_tests(ffmpeg_license_test TEST_PREFIX "ffmpeg_license_test.")
endif()
