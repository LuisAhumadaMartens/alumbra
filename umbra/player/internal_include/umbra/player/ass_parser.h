#ifndef UMBRA_PLAYER_INTERNAL_ASS_PARSER_H_
#define UMBRA_PLAYER_INTERNAL_ASS_PARSER_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "umbra/player/text_track.h"

namespace umbra::player {

struct AssWarning {
  std::size_t line = 0;
  std::string reason;
};

struct AssResult {
  std::vector<TextCue> cues;
  std::vector<AssWarning> warnings;
  int play_res_x = 384;
  int play_res_y = 288;
};

struct AssOptions {
  std::size_t max_script_bytes = 4ULL * 1024ULL * 1024ULL;
  std::size_t max_events = 100000;
  std::size_t max_event_bytes = 65536;
};

class AssParser {
 public:
  [[nodiscard]] static AssResult Parse(std::string_view script,
                                       AssOptions options = {});
  [[nodiscard]] static AssResult ParseMatroskaEvent(
      std::string_view codec_private, std::string_view event, double start_time,
      double end_time, AssOptions options = {});
};

}

#endif
