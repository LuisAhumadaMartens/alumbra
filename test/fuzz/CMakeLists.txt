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

if(ALUMBRA_FUZZ_CANARIES)
  foreach(_kind IN ITEMS crash leak oom timeout)
    alumbra_add_fuzz_canary(${_kind}
      SOURCES canary/${_kind}_canary.cc
      CORPUS canary/corpus)
  endforeach()
endif()
