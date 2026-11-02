add_library(penumbra_reference_player STATIC
  decoder_blocklist.cc
  decoder_chain.cc
  decoder_selection.cc
  drop_attribution.cc)
add_library(penumbra::reference_player ALIAS penumbra_reference_player)
set_target_properties(penumbra_reference_player PROPERTIES
  EXPORT_NAME "penumbra::reference_player")
target_include_directories(penumbra_reference_player PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(penumbra_reference_player PUBLIC
  penumbra::interface
  absl::cord
  absl::status
  absl::statusor
  absl::strings
  absl::time)
alumbra_target_options(penumbra_reference_player)
