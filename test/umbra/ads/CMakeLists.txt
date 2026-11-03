add_executable(umbra_ads_unittests
  ad_manager_test.cc beacon_dispatcher_test.cc provider_test.cc
  obstruction_registry_test.cc vast_parser_test.cc vmap_parser_test.cc)
target_link_libraries(umbra_ads_unittests PRIVATE
  umbra::ads umbra::manifest GTest::gtest_main)
target_link_libraries(umbra_ads_unittests PRIVATE umbra::base_test_support)
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

if(ALUMBRA_PORT STREQUAL "desktop")
  find_package(CURL CONFIG REQUIRED)
  find_package(OpenSSL REQUIRED)
  add_executable(umbra_ads_beacon_http_test
    beacon_http_test.cc
    "${PROJECT_SOURCE_DIR}/test/penumbra_desktop/http_test_server.cc")
  target_link_libraries(umbra_ads_beacon_http_test PRIVATE
    umbra::ads umbra::base_test_support penumbra::desktop_http CURL::libcurl
    OpenSSL::SSL OpenSSL::Crypto Threads::Threads GTest::gtest_main)
  target_include_directories(umbra_ads_beacon_http_test PRIVATE
    "${PROJECT_SOURCE_DIR}/umbra/ads/internal_include"
    "${PROJECT_SOURCE_DIR}/penumbra/desktop/internal_include"
    "${PROJECT_SOURCE_DIR}/test/penumbra_desktop")
  alumbra_target_options(umbra_ads_beacon_http_test)
  gtest_discover_tests(umbra_ads_beacon_http_test
    TEST_PREFIX "umbra_ads_beacon_http_test."
    DISCOVERY_TIMEOUT 60
    PROPERTIES TIMEOUT 300)
endif()
