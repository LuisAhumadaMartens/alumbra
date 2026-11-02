find_package(FFMPEG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DAV1D REQUIRED IMPORTED_TARGET dav1d)

add_library(penumbra_desktop STATIC desktop.cc)
add_library(penumbra::desktop ALIAS penumbra_desktop)
set_target_properties(penumbra_desktop PROPERTIES EXPORT_NAME "penumbra::desktop")
target_link_libraries(penumbra_desktop PUBLIC penumbra::interface)
alumbra_target_options(penumbra_desktop)
install(TARGETS penumbra_desktop EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")

add_library(penumbra_desktop_capability STATIC
  capability_provider.cc
  codec_string.cc
  decoder_table.cc
  software_decoder_probe.cc)
add_library(penumbra::desktop_capability ALIAS penumbra_desktop_capability)
target_include_directories(penumbra_desktop_capability PUBLIC
  "${PROJECT_SOURCE_DIR}")
target_include_directories(penumbra_desktop_capability SYSTEM PUBLIC
  ${FFMPEG_INCLUDE_DIRS})
target_link_directories(penumbra_desktop_capability PUBLIC
  ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(penumbra_desktop_capability
  PUBLIC penumbra::interface ${FFMPEG_LIBRARIES} PkgConfig::DAV1D)
alumbra_target_options(penumbra_desktop_capability)

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

if(ALUMBRA_BUILD_TESTS)
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
  target_link_libraries(ffmpeg_decoder_test PRIVATE
    penumbra_desktop_decode_testing umbra::codec absl::strings
    GTest::gmock GTest::gtest_main)
  alumbra_target_options(ffmpeg_decoder_test)
  gtest_discover_tests(ffmpeg_decoder_test TEST_PREFIX "ffmpeg_decoder_test.")

  add_executable(dav1d_decoder_test dav1d_decoder_test.cc)
  target_link_libraries(dav1d_decoder_test PRIVATE
    penumbra_desktop_decode_testing penumbra::reference_player absl::strings
    GTest::gmock GTest::gtest_main)
  alumbra_target_options(dav1d_decoder_test)
  gtest_discover_tests(dav1d_decoder_test TEST_PREFIX "dav1d_decoder_test.")

  add_executable(ffmpeg_components_test ffmpeg_components_test.cc)
  target_compile_definitions(ffmpeg_components_test PRIVATE
    "ALUMBRA_FFMPEG_COMPONENTS_FILE=\"${PROJECT_SOURCE_DIR}/cmake/ports/ffmpeg/components.txt\"")
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
