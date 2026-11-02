add_library(umbra_base STATIC
  base.cc
  bit_reader.cc
  byte_reader.cc
  lock.cc
  sequence_checker.cc
  sequence_token.cc
  sip_hash.cc
  single_thread_task_executor.cc
  thread_pool.cc
  tick_clock.cc
  status.cc
  url_redactor.cc)
add_library(umbra::base ALIAS umbra_base)
set_target_properties(umbra_base PROPERTIES EXPORT_NAME "umbra::base")
target_include_directories(umbra_base PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(umbra_base
  PUBLIC
    penumbra::interface
    "$<BUILD_INTERFACE:absl::any_invocable>"
    "$<BUILD_INTERFACE:absl::core_headers>"
    "$<BUILD_INTERFACE:absl::time>"
    "$<BUILD_INTERFACE:absl::status>"
    "$<BUILD_INTERFACE:absl::statusor>"
  PRIVATE absl::time absl::status absl::statusor absl::cord absl::strings
    Threads::Threads)
alumbra_target_options(umbra_base)
target_compile_options(umbra_base PRIVATE -fno-exceptions)
install(TARGETS umbra_base EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(ALUMBRA_BUILD_TESTS)
  add_subdirectory(test_support)
endif()
