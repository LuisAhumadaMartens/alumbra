add_library(umbra_player STATIC
  can_play_type.cc
  ass_parser.cc
  ../text/cea_decoder.cc
  language_preferences.cc
  media_error.cc
  player.cc
  player_default.cc
  source_router.cc
  srt_parser.cc
  ../text/ttml_parser.cc
  text_track_routing.cc
  text_track.cc
  webvtt.cc
  time_ranges.cc)
add_library(umbra::player ALIAS umbra_player)
set_target_properties(umbra_player PROPERTIES EXPORT_NAME "umbra::player")
target_include_directories(umbra_player PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_include_directories(umbra_player PRIVATE
  "${PROJECT_SOURCE_DIR}/umbra/text/internal_include")
target_link_libraries(umbra_player
  PUBLIC
    umbra::base
    penumbra::interface
  PRIVATE
    umbra::observability
    umbra::settings
    umbra::pipeline
    umbra::source
    umbra::manifest
    umbra::fetch
    umbra::codec
    umbra::observability
    absl::status
    absl::statusor
    absl::strings
    absl::str_format
    absl::time)
if(ALUMBRA_PORT STREQUAL "desktop")
  target_compile_definitions(umbra_player PRIVATE ALUMBRA_DEFAULT_DESKTOP_PORT=1)
endif()
alumbra_target_options(umbra_player)
target_compile_options(umbra_player PRIVATE -fno-exceptions)
install(TARGETS umbra_player EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
