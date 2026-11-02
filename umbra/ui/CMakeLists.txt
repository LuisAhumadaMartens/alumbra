add_library(umbra_ui STATIC
  controls.cc
  invariants.cc
  keyboard_map.cc
  machine.cc
  reduce.cc
  state.cc
  ui.cc
  view_model.cc)
add_library(umbra::ui ALIAS umbra_ui)
set_target_properties(umbra_ui PROPERTIES EXPORT_NAME "umbra::ui")
target_include_directories(umbra_ui PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>")
target_link_libraries(umbra_ui
  PUBLIC
    penumbra::interface
    "$<BUILD_INTERFACE:umbra::base>"
    "$<BUILD_INTERFACE:absl::any_invocable>"
    "$<BUILD_INTERFACE:absl::core_headers>"
    "$<BUILD_INTERFACE:absl::time>")
alumbra_target_options(umbra_ui)
target_compile_options(umbra_ui PRIVATE -fno-exceptions)
install(TARGETS umbra_ui EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
