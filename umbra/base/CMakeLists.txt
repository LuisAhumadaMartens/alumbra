add_library(umbra_base STATIC
  base.cc
  lock.cc
  sequence_checker.cc
  sequence_token.cc
  single_thread_task_executor.cc
  thread_pool.cc
  tick_clock.cc)
add_library(umbra::base ALIAS umbra_base)
set_target_properties(umbra_base PROPERTIES EXPORT_NAME "umbra::base")
target_include_directories(umbra_base PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(umbra_base
  PUBLIC penumbra::interface absl::any_invocable absl::core_headers absl::time
  PRIVATE Threads::Threads)
alumbra_target_options(umbra_base)
install(TARGETS umbra_base EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(ALUMBRA_BUILD_TESTS)
  add_subdirectory(test_support)
endif()
