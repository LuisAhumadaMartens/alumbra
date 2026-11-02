add_library(umbra_source STATIC
  buffered_reader.cc
  credentials.cc
  read_request.cc
  scheme_registry.cc
  source_error.cc
  source_resolver.cc
  source_type.cc
  source_type_detector.cc
  url.cc)
add_library(umbra::source ALIAS umbra_source)
set_target_properties(umbra_source PROPERTIES EXPORT_NAME "umbra::source")
target_include_directories(umbra_source PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>")
target_link_libraries(umbra_source
  PUBLIC
    umbra::base
    penumbra::interface
    "$<BUILD_INTERFACE:absl::any_invocable>"
    "$<BUILD_INTERFACE:absl::time>"
    "$<BUILD_INTERFACE:absl::status>"
    "$<BUILD_INTERFACE:absl::statusor>"
  PRIVATE absl::strings)
alumbra_target_options(umbra_source)
target_compile_options(umbra_source PRIVATE -fno-exceptions)
install(TARGETS umbra_source EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
