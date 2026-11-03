#ifndef UMBRA_TEXT_TTML_PARSER_H_
#define UMBRA_TEXT_TTML_PARSER_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "umbra/player/text_track.h"

namespace umbra::text {

struct TtmlParserOptions {
  std::size_t max_document_bytes = 4194304;
  std::size_t max_depth = 64;
  std::size_t max_nodes = 100000;
  std::size_t max_cues = 10000;
  double sample_time_offset = 0.0;
};

struct TtmlDocument {
  std::vector<player::TextCue> text_cues;
  std::vector<player::BitmapCue> bitmap_cues;
};

[[nodiscard]] absl::StatusOr<TtmlDocument> ParseTtml(
    std::string_view document, const TtmlParserOptions& options = {});

}

#endif
