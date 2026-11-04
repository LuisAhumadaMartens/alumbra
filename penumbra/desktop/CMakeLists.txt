find_package(FFMPEG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DAV1D REQUIRED IMPORTED_TARGET dav1d)
find_package(SDL3 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
include("${PROJECT_SOURCE_DIR}/cmake/AlumbraShaderBlobs.cmake")

set(ALUMBRA_SHADER_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/video/shaders")
set(ALUMBRA_GENERATED_SHADER_DIRECTORY "${ALUMBRA_SHADER_DIRECTORY}/generated")
set(_shader_blobs_source "${CMAKE_CURRENT_BINARY_DIR}/shader_blobs.cc")
alumbra_generate_shader_blobs("${_shader_blobs_source}"
  "${ALUMBRA_GENERATED_SHADER_DIRECTORY}"
  fullscreen_triangle.vert:FullscreenTriangleVertexShader
  yuv_to_rgb.frag:YuvToRgbFragmentShader
  ui.vert:UiVertexShader
  ui.frag:UiFragmentShader)

add_library(penumbra_desktop STATIC
  av_sync_capture.cc
  audio_converter.cc
  desktop.cc
  video/presentation_timing.cc
  audio/audio_renderer.cc
  stderr_log_sink.cc
  video/color_conversion.cc
  video/plane_layout.cc
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
    "$<BUILD_INTERFACE:absl::status>"
    "$<BUILD_INTERFACE:absl::statusor>"
  PRIVATE
    absl::strings)
target_include_directories(penumbra_desktop SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(penumbra_desktop PRIVATE ${FFMPEG_LIBRARY_DIRS})
alumbra_target_options(penumbra_desktop)

add_library(penumbra_desktop_crypto STATIC openssl_crypto_provider.cc)
add_library(penumbra::desktop_crypto ALIAS penumbra_desktop_crypto)
target_include_directories(penumbra_desktop_crypto PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/internal_include")
target_link_libraries(penumbra_desktop_crypto
  PUBLIC penumbra::interface
  PRIVATE OpenSSL::Crypto)
alumbra_target_options(penumbra_desktop_crypto)
install(TARGETS penumbra_desktop EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

add_library(penumbra_desktop_capability STATIC
  capability_provider.cc
  codec_string.cc
  decoder_table.cc
  software_decoder_probe.cc)
add_library(penumbra::desktop_capability ALIAS penumbra_desktop_capability)
target_include_directories(penumbra_desktop_capability PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>")
target_include_directories(penumbra_desktop_capability SYSTEM PUBLIC
  "$<BUILD_INTERFACE:${FFMPEG_INCLUDE_DIRS}>")
target_link_directories(penumbra_desktop_capability PUBLIC
  "$<BUILD_INTERFACE:${FFMPEG_LIBRARY_DIRS}>")
target_link_libraries(penumbra_desktop_capability
  PUBLIC penumbra::interface ${FFMPEG_LIBRARIES} PkgConfig::DAV1D)
alumbra_target_options(penumbra_desktop_capability)
install(TARGETS penumbra_desktop_capability EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

add_library(penumbra_desktop_decode STATIC
  dav1d_decoder.cc
  decode_errors.cc
  decoder_config.cc
  desktop_component_factory.cc
  ffmpeg_decoder.cc
  ffmpeg_frame.cc
  worker_decoder.cc)
add_library(penumbra::desktop_decode ALIAS penumbra_desktop_decode)
set_target_properties(penumbra_desktop_decode PROPERTIES
  EXPORT_NAME "penumbra::desktop_decode")
target_link_libraries(penumbra_desktop_decode
  PUBLIC
    penumbra::desktop_capability
    penumbra::interface
    absl::status
    absl::statusor
    Threads::Threads
  PRIVATE
    umbra::codec
    absl::strings)
alumbra_target_options(penumbra_desktop_decode)
install(TARGETS penumbra_desktop_decode EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

add_library(penumbra_desktop_fallback STATIC ffmpeg_fallback_demuxer.cc)
add_library(penumbra::desktop_fallback ALIAS penumbra_desktop_fallback)
set_target_properties(penumbra_desktop_fallback PROPERTIES
  EXPORT_NAME "penumbra::desktop_fallback")
target_link_libraries(penumbra_desktop_fallback
  PUBLIC
    penumbra::desktop_capability
    penumbra::interface
    absl::status
    absl::statusor
    Threads::Threads
  PRIVATE
    absl::strings)
alumbra_target_options(penumbra_desktop_fallback)
install(TARGETS penumbra_desktop_fallback EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

add_library(penumbra_desktop_file STATIC file_byte_stream_provider.cc)
add_library(penumbra::desktop_file ALIAS penumbra_desktop_file)
target_include_directories(penumbra_desktop_file PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>")
target_link_libraries(penumbra_desktop_file
  PUBLIC
    penumbra::interface
    absl::status
    absl::statusor
  PRIVATE
    absl::strings
    absl::time
    Threads::Threads)
alumbra_target_options(penumbra_desktop_file)
install(TARGETS penumbra_desktop_file EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

find_package(CURL CONFIG REQUIRED)
add_library(penumbra_desktop_http STATIC curl_http_transport.cc)
add_library(penumbra::desktop_http ALIAS penumbra_desktop_http)
target_include_directories(penumbra_desktop_http PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>")
target_link_libraries(penumbra_desktop_http
  PUBLIC
    penumbra::interface
    absl::status
    absl::statusor
  PRIVATE
    absl::strings
    absl::time
    CURL::libcurl)
alumbra_target_options(penumbra_desktop_http)
install(TARGETS penumbra_desktop_http EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

add_library(penumbra_desktop_port STATIC
  audio/audio_sink.cc
  desktop_port.cc
  video/gpu_presenter.cc
  video/sdl_session.cc)
add_library(penumbra::desktop_port ALIAS penumbra_desktop_port)
target_include_directories(penumbra_desktop_port PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>")
target_link_libraries(penumbra_desktop_port
  PUBLIC penumbra::interface
  PRIVATE
    SDL3::SDL3
    penumbra::desktop
    penumbra::desktop_capability
    penumbra::desktop_decode
    penumbra::desktop_fallback
    penumbra::desktop_file
    penumbra::desktop_http
    penumbra::reference_player
    absl::status
    absl::statusor)
alumbra_target_options(penumbra_desktop_port)
install(TARGETS penumbra_desktop_port EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

if(ALUMBRA_BUILD_TESTS)
  add_executable(presentation_timing_test presentation_timing_test.cc)
  target_link_libraries(presentation_timing_test PRIVATE
    penumbra::desktop GTest::gtest_main)
  alumbra_target_options(presentation_timing_test)
  gtest_discover_tests(presentation_timing_test
    TEST_PREFIX "presentation_timing_test.")

  add_executable(audio_converter_test audio_converter_test.cc)
  target_include_directories(audio_converter_test SYSTEM PRIVATE
    ${FFMPEG_INCLUDE_DIRS})
  target_link_directories(audio_converter_test PRIVATE ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(audio_converter_test PRIVATE
    penumbra::desktop ${FFMPEG_LIBRARIES} GTest::gtest_main)
  alumbra_target_options(audio_converter_test)
  gtest_discover_tests(audio_converter_test TEST_PREFIX "audio_converter_test.")

  add_executable(stderr_log_sink_test stderr_log_sink_test.cc)
  target_link_libraries(stderr_log_sink_test PRIVATE
    penumbra::desktop GTest::gtest_main)
  alumbra_target_options(stderr_log_sink_test)
  gtest_discover_tests(stderr_log_sink_test TEST_PREFIX "stderr_log_sink_test.")

  add_executable(openssl_crypto_provider_test openssl_crypto_provider_test.cc)
  target_link_libraries(openssl_crypto_provider_test PRIVATE
    penumbra::desktop_crypto OpenSSL::Crypto GTest::gtest_main)
  alumbra_target_options(openssl_crypto_provider_test)
  gtest_discover_tests(openssl_crypto_provider_test
    TEST_PREFIX "openssl_crypto_provider_test.")

  add_executable(ffmpeg_license_test ffmpeg_license_test.cc)
  target_include_directories(ffmpeg_license_test SYSTEM PRIVATE
    ${FFMPEG_INCLUDE_DIRS})
  target_link_directories(ffmpeg_license_test PRIVATE ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(ffmpeg_license_test PRIVATE
    ${FFMPEG_LIBRARIES} ${CMAKE_DL_LIBS} GTest::gtest_main)
  alumbra_target_options(ffmpeg_license_test)
  gtest_discover_tests(ffmpeg_license_test TEST_PREFIX "ffmpeg_license_test.")

  add_library(penumbra_desktop_decode_testing STATIC decode_test_util.cc)
  target_compile_definitions(penumbra_desktop_decode_testing PUBLIC
    "ALUMBRA_DECODE_FIXTURE_DIR=\"${PROJECT_SOURCE_DIR}/test/fixtures/decode\"")
  target_link_libraries(penumbra_desktop_decode_testing
    PUBLIC penumbra::desktop_decode absl::strings)
  alumbra_target_options(penumbra_desktop_decode_testing)

  add_executable(ffmpeg_decoder_test ffmpeg_decoder_test.cc)
  target_sources(ffmpeg_decoder_test PRIVATE
    "${PROJECT_SOURCE_DIR}/test/umbra/demux/mp4_builder.cc"
    "${PROJECT_SOURCE_DIR}/test/umbra/demux/test_support.cc")
  target_include_directories(ffmpeg_decoder_test PRIVATE
    "${PROJECT_SOURCE_DIR}/test/umbra/demux")
  target_compile_definitions(ffmpeg_decoder_test PRIVATE
    "ALUMBRA_DEMUX_FIXTURE_DIR=\"${PROJECT_SOURCE_DIR}/test/umbra/demux/fixtures\"")
  target_link_libraries(ffmpeg_decoder_test PRIVATE
    penumbra_desktop_decode_testing umbra::codec absl::strings
    umbra::demux GTest::gmock GTest::gtest_main)
  alumbra_target_options(ffmpeg_decoder_test)
  gtest_discover_tests(ffmpeg_decoder_test TEST_PREFIX "ffmpeg_decoder_test.")

  add_executable(ffmpeg_decoder_conformance_test
    ffmpeg_decoder_conformance_test.cc)
  target_link_libraries(ffmpeg_decoder_conformance_test PRIVATE
    penumbra_desktop_decode_testing)
  alumbra_target_options(ffmpeg_decoder_conformance_test)

  add_executable(ffmpeg_fallback_demuxer_test ffmpeg_fallback_demuxer_test.cc)
  target_compile_definitions(ffmpeg_fallback_demuxer_test PRIVATE
    "ALUMBRA_FALLBACK_FIXTURE_DIR=\"${PROJECT_SOURCE_DIR}/test/fixtures/fallback_demux\""
    "ALUMBRA_DEMUX_FIXTURE_DIR=\"${PROJECT_SOURCE_DIR}/test/umbra/demux/fixtures\"")
  target_link_libraries(ffmpeg_fallback_demuxer_test PRIVATE
    penumbra::desktop_fallback umbra::demux umbra::settings absl::strings
    GTest::gmock GTest::gtest_main)
  alumbra_target_options(ffmpeg_fallback_demuxer_test)
  gtest_discover_tests(ffmpeg_fallback_demuxer_test
    TEST_PREFIX "ffmpeg_fallback_demuxer_test."
    DISCOVERY_TIMEOUT 60
    PROPERTIES TIMEOUT 300)

  add_executable(dav1d_decoder_test dav1d_decoder_test.cc)
  target_link_libraries(dav1d_decoder_test PRIVATE
    penumbra_desktop_decode_testing penumbra::reference_player absl::strings
    GTest::gmock GTest::gtest_main)
  alumbra_target_options(dav1d_decoder_test)
  gtest_discover_tests(dav1d_decoder_test TEST_PREFIX "dav1d_decoder_test.")

  add_executable(ffmpeg_components_test ffmpeg_components_test.cc)
  target_compile_definitions(ffmpeg_components_test PRIVATE
    "ALUMBRA_FFMPEG_COMPONENTS_FILE=\"${PROJECT_SOURCE_DIR}/cmake/ports/ffmpeg/components.txt\"")
  target_compile_definitions(ffmpeg_components_test PRIVATE
    "ALUMBRA_FFMPEG_PATENT_DECODERS=$<BOOL:${ALUMBRA_FFMPEG_PATENT_DECODERS}>")
  target_include_directories(ffmpeg_components_test SYSTEM PRIVATE
    ${FFMPEG_INCLUDE_DIRS})
  target_link_directories(ffmpeg_components_test PRIVATE ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(ffmpeg_components_test PRIVATE
    ${FFMPEG_LIBRARIES} absl::strings GTest::gmock GTest::gtest_main)
  alumbra_target_options(ffmpeg_components_test)
  gtest_discover_tests(ffmpeg_components_test
    TEST_PREFIX "ffmpeg_components_test.")

  add_executable(capability_provider_test capability_provider_test.cc)
  target_link_libraries(capability_provider_test PRIVATE
    penumbra::desktop_capability GTest::gtest_main)
  alumbra_target_options(capability_provider_test)
  gtest_discover_tests(capability_provider_test
    TEST_PREFIX "capability_provider_test.")

  add_executable(capability_provider_benchmark
    capability_provider_benchmark.cc)
  target_link_libraries(capability_provider_benchmark PRIVATE
    penumbra::desktop_capability benchmark::benchmark)
  alumbra_target_options(capability_provider_benchmark)
  add_test(NAME capability_provider_benchmark
    COMMAND capability_provider_benchmark --benchmark_min_time=0.01s)
  set_tests_properties(capability_provider_benchmark PROPERTIES LABELS benchmark)
endif()
