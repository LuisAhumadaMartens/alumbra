add_executable(umbra_ads_unittests ad_manager_test.cc vast_parser_test.cc
  vmap_parser_test.cc)
target_link_libraries(umbra_ads_unittests PRIVATE
  umbra::ads umbra::manifest GTest::gtest_main)
target_include_directories(umbra_ads_unittests PRIVATE
  "${PROJECT_SOURCE_DIR}/umbra/ads/internal_include")
target_compile_definitions(umbra_ads_unittests PRIVATE
  ALUMBRA_ADS_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/test/umbra/manifest/fixtures"
  ALUMBRA_VAST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
alumbra_target_options(umbra_ads_unittests)
target_compile_options(umbra_ads_unittests PRIVATE -fno-exceptions)
gtest_discover_tests(umbra_ads_unittests
  TEST_PREFIX "umbra_ads_unittests."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300)
