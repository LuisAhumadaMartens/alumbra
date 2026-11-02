add_library(penumbra_conformance_support STATIC control_runner_adapter.cc)
add_library(penumbra::conformance_support ALIAS penumbra_conformance_support)
target_include_directories(penumbra_conformance_support PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(penumbra_conformance_support PUBLIC
  penumbra::interface umbra::base)
alumbra_target_options(penumbra_conformance_support)

add_library(penumbra_conformance_kit OBJECT
  conformance_fixture.cc
  report.cc
  lifecycle_suite.cc
  flow_control_suite.cc
  seek_ticket_suite.cc
  clock_suite.cc
  callback_threading_suite.cc
  error_reporting_suite.cc
  capability_suite.cc
  output_suite.cc
  component_suite.cc)
add_library(penumbra::conformance_kit ALIAS penumbra_conformance_kit)
target_link_libraries(penumbra_conformance_kit PUBLIC
  penumbra::conformance_support penumbra::interface umbra::base_test_support GTest::gtest)
alumbra_target_options(penumbra_conformance_kit)

add_library(penumbra_conformance_main OBJECT conformance_main.cc)
add_library(penumbra::conformance_main ALIAS penumbra_conformance_main)
target_link_libraries(penumbra_conformance_main PUBLIC
  penumbra::conformance_kit GTest::gtest)
alumbra_target_options(penumbra_conformance_main)
