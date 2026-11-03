add_library(umbra_settings STATIC
  setting_parser.cc
  setting_value.cc
  settings_registry.cc
  store.cc)
add_library(umbra::settings ALIAS umbra_settings)
set_target_properties(umbra_settings PROPERTIES EXPORT_NAME "umbra::settings")
target_include_directories(umbra_settings PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>")
target_link_libraries(umbra_settings
  PUBLIC umbra::base absl::any_invocable absl::status absl::statusor absl::time
  PRIVATE absl::strings)
alumbra_target_options(umbra_settings)
target_compile_options(umbra_settings PRIVATE -fno-exceptions)
install(TARGETS umbra_settings EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
