add_library(penumbra_reference_player STATIC
  decoder_blocklist.cc
  decoder_chain.cc
  decoder_selection.cc
  drop_attribution.cc
  media_clock.cc
  player.cc
  video_cadence.cc
  video_frame_selector.cc)
add_library(penumbra::reference_player ALIAS penumbra_reference_player)
set_target_properties(penumbra_reference_player PROPERTIES
  EXPORT_NAME "penumbra::reference_player")
target_include_directories(penumbra_reference_player PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>")
target_link_libraries(penumbra_reference_player PUBLIC
  penumbra::interface
  absl::cord
  absl::status
  absl::statusor
  absl::strings
  absl::time)
alumbra_target_options(penumbra_reference_player)
install(TARGETS penumbra_reference_player EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
