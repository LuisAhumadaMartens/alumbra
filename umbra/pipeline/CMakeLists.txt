add_library(umbra_pipeline STATIC
  buffer_manager.cc
  event_queue.cc
  media_events.cc
  adaptive_source.cc
  progressive_source.cc
  playback_state.cc
  playback_state_machine.cc)
add_library(umbra::pipeline ALIAS umbra_pipeline)
set_target_properties(umbra_pipeline PROPERTIES EXPORT_NAME "umbra::pipeline")
target_include_directories(umbra_pipeline PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>"
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/umbra/player/include>")
target_link_libraries(umbra_pipeline
  PUBLIC
    umbra::base
    penumbra::interface
    umbra::demux
    umbra::source
    umbra::fetch
    umbra::manifest
    absl::status
    absl::statusor)
alumbra_target_options(umbra_pipeline)
target_compile_options(umbra_pipeline PRIVATE -fno-exceptions)
install(TARGETS umbra_pipeline EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
