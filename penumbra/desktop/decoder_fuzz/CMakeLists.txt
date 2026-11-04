find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavutil)

add_library(penumbra_ffmpeg_fuzz_decode STATIC
  ../decode_errors.cc
  ../decoder_config.cc
  ../ffmpeg_decoder.cc
  ../ffmpeg_frame.cc
  ../worker_decoder.cc)
target_include_directories(penumbra_ffmpeg_fuzz_decode PUBLIC
  "${PROJECT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../internal_include")
target_include_directories(penumbra_ffmpeg_fuzz_decode SYSTEM PUBLIC
  ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(penumbra_ffmpeg_fuzz_decode
  PUBLIC penumbra::interface
  PRIVATE
    PkgConfig::FFMPEG
    umbra::codec
    absl::status
    absl::statusor
    absl::strings
    Threads::Threads)
alumbra_target_options(penumbra_ffmpeg_fuzz_decode)
