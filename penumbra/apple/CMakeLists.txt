add_library(penumbra_apple_http STATIC url_session_http_transport.mm)
add_library(penumbra::apple_http ALIAS penumbra_apple_http)
target_include_directories(penumbra_apple_http PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/internal_include")
target_link_libraries(penumbra_apple_http
  PUBLIC penumbra::interface absl::status absl::statusor absl::time
  PRIVATE absl::strings "-framework Foundation")
alumbra_target_options(penumbra_apple_http)
target_compile_options(penumbra_apple_http PRIVATE
  "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")

if(ALUMBRA_BUILD_TESTS)
  add_executable(penumbra_apple_http_test url_session_http_transport_test.mm)
  target_link_libraries(penumbra_apple_http_test PRIVATE
    penumbra::apple_http GTest::gtest_main Threads::Threads)
  alumbra_target_options(penumbra_apple_http_test)
  target_compile_options(penumbra_apple_http_test PRIVATE
    "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
  gtest_discover_tests(penumbra_apple_http_test
    TEST_PREFIX "penumbra_apple_http_test."
    DISCOVERY_TIMEOUT 60
    PROPERTIES TIMEOUT 300)
endif()
