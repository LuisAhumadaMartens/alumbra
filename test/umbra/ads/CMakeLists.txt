add_executable(umbra_ads_unittests
  ad_manager_test.cc beacon_dispatcher_test.cc provider_test.cc
  obstruction_registry_test.cc media_file_selector_test.cc vast_parser_test.cc
  vast_macros_test.cc vmap_parser_test.cc vast_tracking_test.cc)
target_link_libraries(umbra_ads_unittests PRIVATE
  umbra::ads umbra::manifest GTest::gtest_main)
target_link_libraries(umbra_ads_unittests PRIVATE umbra::base_test_support)
