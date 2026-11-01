add_library(alumbra_design_tokens INTERFACE)
add_library(alumbra::design_tokens ALIAS alumbra_design_tokens)
set_target_properties(alumbra_design_tokens PROPERTIES
  EXPORT_NAME "alumbra::design_tokens")
target_include_directories(alumbra_design_tokens INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_features(alumbra_design_tokens INTERFACE cxx_std_20)
install(TARGETS alumbra_design_tokens EXPORT AlumbraTargets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
