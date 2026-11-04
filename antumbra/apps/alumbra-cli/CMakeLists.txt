configure_file(
  "${PROJECT_SOURCE_DIR}/docs/reference/settings/index.json"
  "${CMAKE_CURRENT_BINARY_DIR}/settings-index.json" COPYONLY)

add_executable(alumbra-cli main.cc args.cc)
target_include_directories(alumbra-cli PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(alumbra-cli PRIVATE
  ALUMBRA_CLI_SETTINGS_INDEX="${CMAKE_CURRENT_BINARY_DIR}/settings-index.json")
target_link_libraries(alumbra-cli PRIVATE alumbra::player)
if(ALUMBRA_BUILD_UI)
  target_link_libraries(alumbra-cli PRIVATE alumbra::desktop_ui)
  target_compile_definitions(alumbra-cli PRIVATE ALUMBRA_CLI_DESKTOP_UI=1)
endif()
alumbra_target_options(alumbra-cli)
