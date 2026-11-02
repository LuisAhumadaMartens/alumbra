add_library(umbra_codec STATIC
  aac.cc
  ac3.cc
  av1.cc
  avc.cc
  codec_string.cc
  flac.cc
  hevc.cc
  nal_unit.cc
  opus.cc
  reader_util.cc
  video_geometry.cc
  vp9.cc)
add_library(umbra::codec ALIAS umbra_codec)
set_target_properties(umbra_codec PROPERTIES EXPORT_NAME "umbra::codec")
target_include_directories(umbra_codec PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>")
target_link_libraries(umbra_codec
  PUBLIC
    umbra::base
    penumbra::interface
  PRIVATE absl::status absl::statusor absl::strings absl::str_format)
alumbra_target_options(umbra_codec)
target_compile_options(umbra_codec PRIVATE -fno-exceptions)
install(TARGETS umbra_codec EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
