add_library(umbra_demux STATIC
  byte_queue.cc
  caption_extractor.cc
  ebml.cc
  mkv_block.cc
  mkv_demuxer.cc
  mkv_elements.cc
  mkv_emit.cc
  mkv_seek_map.cc
  mkv_tracks.cc
  mp4_box.cc
  mp4_demuxer.cc
  mp4_fragment.cc
  mp4_movie.cc
  mp4_samples.cc
  mp4_sample_entry.cc
  mp4_seek_map.cc
  packed_audio_demuxer.cc)
add_library(umbra::demux ALIAS umbra_demux)
set_target_properties(umbra_demux PROPERTIES EXPORT_NAME "umbra::demux")
target_include_directories(umbra_demux PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>")
target_link_libraries(umbra_demux
  PUBLIC
    umbra::base
    umbra::codec
    penumbra::interface
    "$<BUILD_INTERFACE:absl::status>"
    "$<BUILD_INTERFACE:absl::statusor>"
  PRIVATE absl::strings)
alumbra_target_options(umbra_demux)
target_compile_options(umbra_demux PRIVATE -fno-exceptions)
install(TARGETS umbra_demux EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
