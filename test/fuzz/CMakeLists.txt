alumbra_add_fuzz_target(
  NAME byte_reader_fuzzer
  SOURCES byte_reader_fuzzer/byte_reader_fuzzer.cc
  CORPUS byte_reader_fuzzer/corpus byte_reader_fuzzer/regressions
  DICT byte_reader_fuzzer/byte_reader_fuzzer.dict
  LIBRARIES umbra::base)

alumbra_add_fuzz_target(
  NAME error_detail_fuzzer
  SOURCES error_detail_fuzzer/error_detail_fuzzer.cc
  CORPUS error_detail_fuzzer/corpus
  DICT error_detail_fuzzer/error_detail_fuzzer.dict
  LIBRARIES umbra::base absl::cord)

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

if(ALUMBRA_FUZZ_CANARIES)
  foreach(_kind IN ITEMS crash leak oom timeout)
    alumbra_add_fuzz_canary(${_kind}
      SOURCES canary/${_kind}_canary.cc
      CORPUS canary/corpus)
  endforeach()
endif()
