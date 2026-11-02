add_library(umbra_abr STATIC
  rendition_filter.cc)
add_library(umbra::abr ALIAS umbra_abr)
set_target_properties(umbra_abr PROPERTIES EXPORT_NAME "umbra::abr")
target_include_directories(umbra_abr PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>")
target_link_libraries(umbra_abr
  PUBLIC
    umbra::manifest
    umbra::settings
    penumbra::interface
  PRIVATE absl::status absl::statusor)
alumbra_target_options(umbra_abr)
target_compile_options(umbra_abr PRIVATE -fno-exceptions)
