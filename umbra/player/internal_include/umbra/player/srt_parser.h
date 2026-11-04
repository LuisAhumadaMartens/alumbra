#ifndef UMBRA_PLAYER_SRT_PARSER_H_
#define UMBRA_PLAYER_SRT_PARSER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace umbra::player::internal {

struct SrtStyledTextRun {
  std::string text;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  std::string color;
};

struct SrtCue {
  uint64_t id = 0;
  double start_time = 0.0;
  double end_time = 0.0;
  std::vector<SrtStyledTextRun> runs;
  double line = 0.0;
  double position = 50.0;
};

struct SrtParseOptions {
  std::string fallback_encoding;
  std::string encoding_override;
  bool auto_detect = false;
};

struct SrtParseResult {
  std::vector<SrtCue> cues;
  std::string detected_encoding;
  double encoding_confidence = 0.0;
  bool decoded = false;
};

class SrtParser {
 public:
  explicit SrtParser(SrtParseOptions options = {});
  bool Feed(std::string_view bytes);
  SrtParseResult Finish();

 private:
  SrtParseOptions options_;
  std::string bytes_;
  bool failed_ = false;
};

SrtParseResult ParseSubRip(std::string_view bytes,
                           SrtParseOptions options = {});

}

#endif
