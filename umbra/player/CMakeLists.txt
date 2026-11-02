add_library(umbra_player STATIC
  can_play_type.cc
  language_preferences.cc
  media_error.cc
  player.cc
  time_ranges.cc)
add_library(umbra::player ALIAS umbra_player)
set_target_properties(umbra_player PROPERTIES EXPORT_NAME "umbra::player")
target_include_directories(umbra_player PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(umbra_player
  PUBLIC
    umbra::base
    penumbra::interface
  PRIVATE
    umbra::pipeline
    umbra::codec
    absl::status
    absl::statusor
    absl::strings
    absl::str_format
    absl::time)
alumbra_target_options(umbra_player)
target_compile_options(umbra_player PRIVATE -fno-exceptions)
install(TARGETS umbra_player EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
