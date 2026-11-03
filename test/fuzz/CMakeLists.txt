alumbra_add_fuzz_target(
  NAME byte_reader_fuzzer
  SOURCES byte_reader_fuzzer/byte_reader_fuzzer.cc
  CORPUS byte_reader_fuzzer/corpus byte_reader_fuzzer/regressions
  DICT byte_reader_fuzzer/byte_reader_fuzzer.dict
  LIBRARIES umbra::base)

alumbra_add_fuzz_target(
  NAME codec_config_fuzzer
  SOURCES codec_config_fuzzer/codec_config_fuzzer.cc
  CORPUS codec_config_fuzzer/corpus
  DICT codec_config_fuzzer/codec_config_fuzzer.dict
  LIBRARIES umbra::codec)

alumbra_add_fuzz_target(
  NAME error_detail_fuzzer
  SOURCES error_detail_fuzzer/error_detail_fuzzer.cc
  CORPUS error_detail_fuzzer/corpus
  DICT error_detail_fuzzer/error_detail_fuzzer.dict
  LIBRARIES umbra::base absl::cord)

alumbra_add_fuzz_target(
  NAME mp4_demuxer_fuzzer
  SOURCES mp4_demuxer_fuzzer/mp4_demuxer_fuzzer.cc
  CORPUS mp4_demuxer_fuzzer/corpus
  DICT mp4_demuxer_fuzzer/mp4_demuxer_fuzzer.dict
  LIBRARIES umbra::demux)

alumbra_add_fuzz_target(
  NAME packed_audio_demuxer_fuzzer
  SOURCES packed_audio_demuxer_fuzzer/packed_audio_demuxer_fuzzer.cc
  CORPUS packed_audio_demuxer_fuzzer/corpus
  LIBRARIES umbra::demux)

alumbra_add_fuzz_target(
  NAME fmp4_fragment_fuzzer
  SOURCES fmp4_fragment_fuzzer/fmp4_fragment_fuzzer.cc
  CORPUS fmp4_fragment_fuzzer/corpus
  DICT fmp4_fragment_fuzzer/fmp4_fragment_fuzzer.dict
  LIBRARIES umbra::demux)

alumbra_add_fuzz_target(
  NAME ts_demuxer_fuzzer
  SOURCES ts_demuxer_fuzzer/ts_demuxer_fuzzer.cc
  CORPUS ts_demuxer_fuzzer/corpus
  DICT ts_demuxer_fuzzer/ts_demuxer_fuzzer.dict
  LIBRARIES umbra::demux)

alumbra_add_fuzz_target(
  NAME ts_psi_fuzzer
  SOURCES ts_psi_fuzzer/ts_psi_fuzzer.cc
  CORPUS ts_psi_fuzzer/corpus
  DICT ts_psi_fuzzer/ts_psi_fuzzer.dict
  LIBRARIES umbra::demux)

alumbra_add_fuzz_target(
  NAME ebml_fuzzer
  SOURCES ebml_fuzzer/ebml_fuzzer.cc
  CORPUS ebml_fuzzer/corpus
  DICT ebml_fuzzer/ebml_fuzzer.dict
  LIBRARIES umbra::demux)

alumbra_add_fuzz_target(
  NAME mkv_demuxer_fuzzer
  SOURCES mkv_demuxer_fuzzer/mkv_demuxer_fuzzer.cc
  CORPUS mkv_demuxer_fuzzer/corpus
  DICT mkv_demuxer_fuzzer/mkv_demuxer_fuzzer.dict
  LIBRARIES umbra::demux)

alumbra_add_fuzz_target(
  NAME manifest_fuzzer
  SOURCES manifest_fuzzer/manifest_fuzzer.cc
  CORPUS manifest_fuzzer/corpus
  LIBRARIES umbra::manifest)

alumbra_add_fuzz_target(
  NAME vast_parser_fuzzer
  SOURCES vast_parser_fuzzer/vast_parser_fuzzer.cc
  CORPUS vast_parser_fuzzer/corpus
  LIBRARIES umbra::ads)
target_include_directories(vast_parser_fuzzer_replay PRIVATE
  "${PROJECT_SOURCE_DIR}/umbra/ads/internal_include")
if(ALUMBRA_FUZZING)
  target_include_directories(vast_parser_fuzzer PRIVATE
    "${PROJECT_SOURCE_DIR}/umbra/ads/internal_include")
endif()

alumbra_add_fuzz_target(
  NAME vmap_parser_fuzzer
  SOURCES vmap_parser_fuzzer/vmap_parser_fuzzer.cc
  CORPUS vmap_parser_fuzzer/corpus
  LIBRARIES umbra::ads)
target_include_directories(vmap_parser_fuzzer_replay PRIVATE
  "${PROJECT_SOURCE_DIR}/umbra/ads/internal_include")
if(ALUMBRA_FUZZING)
  target_include_directories(vmap_parser_fuzzer PRIVATE
    "${PROJECT_SOURCE_DIR}/umbra/ads/internal_include")
endif()

alumbra_add_fuzz_target(
  NAME settings_value_parser_fuzzer
  SOURCES settings_value_parser_fuzzer/settings_value_parser_fuzzer.cc
  CORPUS settings_value_parser_fuzzer/corpus
  DICT settings_value_parser_fuzzer/settings_value_parser_fuzzer.dict
  LIBRARIES umbra::settings)

alumbra_add_fuzz_target(
  NAME url_parser_fuzzer
  SOURCES url_parser_fuzzer/url_parser_fuzzer.cc
  CORPUS url_parser_fuzzer/corpus
  DICT url_parser_fuzzer/url_parser_fuzzer.dict
  LIBRARIES umbra::source)

alumbra_add_fuzz_target(
  NAME source_sniffer_fuzzer
  SOURCES source_sniffer_fuzzer/source_sniffer_fuzzer.cc
  CORPUS source_sniffer_fuzzer/corpus
  LIBRARIES umbra::source)

alumbra_add_fuzz_target(
  NAME abr_trace_fuzzer
  SOURCES abr_trace_fuzzer/abr_trace_fuzzer.cc
    ../umbra/abr/simulator.cc
  CORPUS abr_trace_fuzzer/corpus
  LIBRARIES umbra::abr umbra::base_test_support)

alumbra_add_fuzz_target(
  NAME srt_parser_fuzzer
  SOURCES srt_parser_fuzzer/srt_parser_fuzzer.cc
  CORPUS srt_parser_fuzzer/corpus
  LIBRARIES umbra::player)

alumbra_add_fuzz_target(
  NAME webvtt_fuzzer
  SOURCES webvtt_fuzzer/webvtt_fuzzer.cc
  CORPUS webvtt_fuzzer/corpus
  LIBRARIES umbra::player)

if(ALUMBRA_FUZZ_CANARIES)
  foreach(_kind IN ITEMS crash leak oom timeout)
    alumbra_add_fuzz_canary(${_kind}
      SOURCES canary/${_kind}_canary.cc
      CORPUS canary/corpus)
  endforeach()
endif()
