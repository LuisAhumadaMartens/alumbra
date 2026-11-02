add_library(umbra_fetch STATIC
  init_cache.cc
  segment_fetcher.cc)
add_library(umbra::fetch ALIAS umbra_fetch)
set_target_properties(umbra_fetch PROPERTIES EXPORT_NAME "umbra::fetch")
target_include_directories(umbra_fetch PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>")
target_link_libraries(umbra_fetch
  PUBLIC
    umbra::base
    umbra::manifest
    penumbra::interface
    "$<BUILD_INTERFACE:absl::any_invocable>"
    "$<BUILD_INTERFACE:absl::time>"
    "$<BUILD_INTERFACE:absl::status>"
    "$<BUILD_INTERFACE:absl::statusor>"
  PRIVATE
    umbra::source
    absl::strings)
alumbra_target_options(umbra_fetch)
target_compile_options(umbra_fetch PRIVATE -fno-exceptions)
install(TARGETS umbra_fetch EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
