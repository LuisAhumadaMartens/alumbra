add_library(umbra_settings_all_types STATIC
  all_types/all_types_registry.cc)
target_include_directories(umbra_settings_all_types PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/all_types/include")
target_link_libraries(umbra_settings_all_types PUBLIC umbra::settings)
alumbra_target_options(umbra_settings_all_types)
target_compile_options(umbra_settings_all_types PRIVATE -fno-exceptions)

add_executable(umbra_settings_unittests
  all_types_test.cc
  concurrency_test.cc
  parser_test.cc
  registry_test.cc
  store_test.cc)
target_link_libraries(umbra_settings_unittests PRIVATE
  umbra::settings umbra_settings_all_types umbra::base_test_support
  GTest::gtest_main Threads::Threads)
alumbra_target_options(umbra_settings_unittests)
target_compile_options(umbra_settings_unittests PRIVATE -fno-exceptions)
gtest_discover_tests(umbra_settings_unittests
  TEST_PREFIX "umbra_settings_unittests."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300)
