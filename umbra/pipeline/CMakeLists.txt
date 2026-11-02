add_library(umbra_pipeline STATIC
  event_queue.cc
  media_events.cc
  playback_state.cc
  playback_state_machine.cc)
add_library(umbra::pipeline ALIAS umbra_pipeline)
set_target_properties(umbra_pipeline PROPERTIES EXPORT_NAME "umbra::pipeline")
target_include_directories(umbra_pipeline PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/internal_include")
target_link_libraries(umbra_pipeline
  PUBLIC
    umbra::base
    penumbra::interface
    absl::status
    absl::statusor)
alumbra_target_options(umbra_pipeline)
target_compile_options(umbra_pipeline PRIVATE -fno-exceptions)
