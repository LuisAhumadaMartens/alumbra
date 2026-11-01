add_library(penumbra_interface INTERFACE)
add_library(penumbra::interface ALIAS penumbra_interface)
set_target_properties(penumbra_interface PROPERTIES EXPORT_NAME "penumbra::interface")
target_compile_features(penumbra_interface INTERFACE cxx_std_20)
target_include_directories(penumbra_interface INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(penumbra_interface INTERFACE
  "$<BUILD_INTERFACE:absl::statusor>"
  "$<BUILD_INTERFACE:absl::any_invocable>"
  "$<BUILD_INTERFACE:absl::time>")
install(TARGETS penumbra_interface EXPORT AlumbraTargets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
