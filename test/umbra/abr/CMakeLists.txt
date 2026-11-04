add_executable(umbra_abr_unittests
  bandwidth_estimator_test.cc
  simulation_test.cc
  simulator.cc
  policy_test.cc
  rendition_filter_test.cc)
target_link_libraries(umbra_abr_unittests PRIVATE
  umbra::abr umbra::base_test_support GTest::gtest_main)
target_compile_definitions(umbra_abr_unittests PRIVATE
  ALUMBRA_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
alumbra_target_options(umbra_abr_unittests)
gtest_discover_tests(umbra_abr_unittests
  TEST_PREFIX "umbra_abr_unittests."
  DISCOVERY_TIMEOUT 60
  PROPERTIES TIMEOUT 300)

add_executable(abr_simulator simulator_main.cc simulator.cc)
target_link_libraries(abr_simulator PRIVATE umbra::abr umbra::base_test_support)
alumbra_target_options(abr_simulator)
add_test(NAME abr_simulator_synthetic_suite
  COMMAND abr_simulator --suite "${CMAKE_CURRENT_SOURCE_DIR}/traces"
    --baseline "${CMAKE_CURRENT_SOURCE_DIR}/baseline.csv")
