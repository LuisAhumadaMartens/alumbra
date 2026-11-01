add_library(umbra_base_test_support STATIC
  fake_tick_clock.cc
  test_task_environment.cc
  waitable_event.cc)
add_library(umbra::base_test_support ALIAS umbra_base_test_support)
set_target_properties(umbra_base_test_support PROPERTIES
  EXPORT_NAME "umbra::base_test_support")
target_include_directories(umbra_base_test_support PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(umbra_base_test_support PUBLIC umbra::base)
alumbra_target_options(umbra_base_test_support)
