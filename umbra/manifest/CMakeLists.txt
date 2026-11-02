add_library(umbra_manifest STATIC
  dash_mpd_parser.cc
  ids.cc
  manifest_error.cc
  presentation.cc
  refresh.cc
  segment_index.cc
  url_template.cc
  validate.cc)
add_library(umbra::manifest ALIAS umbra_manifest)
set_target_properties(umbra_manifest PROPERTIES EXPORT_NAME "umbra::manifest")
target_include_directories(umbra_manifest PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>")
target_link_libraries(umbra_manifest
  PUBLIC
    umbra::base
    umbra::codec
    penumbra::interface
    "$<BUILD_INTERFACE:absl::time>"
    "$<BUILD_INTERFACE:absl::status>"
    "$<BUILD_INTERFACE:absl::statusor>"
  PRIVATE umbra::observability absl::strings)
alumbra_target_options(umbra_manifest)
target_compile_options(umbra_manifest PRIVATE -fno-exceptions)
install(TARGETS umbra_manifest EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
