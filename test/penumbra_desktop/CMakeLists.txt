add_executable(penumbra_desktop_video_test video_conversion_test.cc)
target_link_libraries(penumbra_desktop_video_test PRIVATE
  penumbra::desktop GTest::gtest_main)
alumbra_target_options(penumbra_desktop_video_test)
gtest_discover_tests(penumbra_desktop_video_test
  TEST_PREFIX "penumbra_desktop_video_test.")

add_executable(penumbra_desktop_port_test desktop_port_test.cc)
target_link_libraries(penumbra_desktop_port_test PRIVATE
  penumbra::desktop_port GTest::gtest_main)
alumbra_target_options(penumbra_desktop_port_test)
gtest_discover_tests(penumbra_desktop_port_test
  TEST_PREFIX "penumbra_desktop_port_test.")

add_executable(penumbra_desktop_conformance_test
  desktop_conformance_test.cc)
target_include_directories(penumbra_desktop_conformance_test PRIVATE
  "${PROJECT_SOURCE_DIR}/test/penumbra_conformance/include")
target_link_libraries(penumbra_desktop_conformance_test PRIVATE
  penumbra::desktop_port penumbra::conformance_kit
  penumbra::conformance_main umbra::base_test_support GTest::gtest)
alumbra_target_options(penumbra_desktop_conformance_test)
gtest_discover_tests(penumbra_desktop_conformance_test
  TEST_PREFIX "penumbra_desktop_conformance.")

add_executable(penumbra_desktop_audio_test
  audio_renderer_test.cc
  audio_sink_test.cc)
target_link_libraries(penumbra_desktop_audio_test PRIVATE
  penumbra::desktop GTest::gtest_main)
alumbra_target_options(penumbra_desktop_audio_test)
gtest_discover_tests(penumbra_desktop_audio_test
  TEST_PREFIX "penumbra_desktop_audio_test.")

if(ALUMBRA_SANITIZER STREQUAL "none")
  add_executable(penumbra_desktop_audio_allocation_test
    audio_allocation_test.cc)
  target_link_libraries(penumbra_desktop_audio_allocation_test PRIVATE
    penumbra::desktop GTest::gtest_main)
  alumbra_target_options(penumbra_desktop_audio_allocation_test)
  gtest_discover_tests(penumbra_desktop_audio_allocation_test
    TEST_PREFIX "penumbra_desktop_audio_allocation_test.")
endif()

add_executable(penumbra_desktop_golden_test video_golden_test.cc)
target_link_libraries(penumbra_desktop_golden_test PRIVATE
  penumbra::desktop GTest::gtest_main)
alumbra_target_options(penumbra_desktop_golden_test)
add_executable(penumbra_desktop_file_test file_byte_stream_provider_test.cc)
target_link_libraries(penumbra_desktop_file_test PRIVATE
  penumbra::desktop_file umbra::source GTest::gtest_main)
target_compile_definitions(penumbra_desktop_file_test PRIVATE
  ALUMBRA_FILE_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/file_provider")
alumbra_target_options(penumbra_desktop_file_test)
gtest_discover_tests(penumbra_desktop_file_test
  TEST_PREFIX "penumbra_desktop_file_test."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300)

find_package(CURL CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
add_executable(penumbra_desktop_http_test
  curl_http_transport_test.cc
  http_test_server.cc)
target_link_libraries(penumbra_desktop_http_test PRIVATE
  penumbra::desktop_http umbra::source CURL::libcurl
  OpenSSL::SSL OpenSSL::Crypto Threads::Threads GTest::gtest_main)
alumbra_target_options(penumbra_desktop_http_test)
gtest_discover_tests(penumbra_desktop_http_test
  TEST_PREFIX "penumbra_desktop_http_test."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300)

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
