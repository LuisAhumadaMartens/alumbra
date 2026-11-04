#ifndef UMBRA_PLAYER_INTERNAL_WEBVTT_PARSER_H_
#define UMBRA_PLAYER_INTERNAL_WEBVTT_PARSER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "umbra/player/text_track.h"

namespace umbra::player {

struct WebVttRegion {
  std::string id;
  double width = 100.0;
  uint32_t lines = 3;
  double region_anchor_x = 0.0;
  double region_anchor_y = 100.0;
  double viewport_anchor_x = 0.0;
  double viewport_anchor_y = 100.0;
  bool scroll_up = false;
};

struct WebVttStyle {
  std::string selector;
  std::string declarations;
};

struct WebVttWarning {
  size_t line = 0;
  std::string reason;
};

struct WebVttResult {
  std::vector<TextCue> cues;
  std::vector<WebVttRegion> regions;
  std::vector<WebVttStyle> styles;
  std::vector<std::string> header_metadata;
  std::vector<WebVttWarning> warnings;
};

struct WebVttOptions {
  size_t max_cue_text_bytes = 65536;
  size_t max_line_bytes = 65536;
  size_t max_cues = 100000;
  size_t max_nesting_depth = 64;
  bool enable_style_blocks = true;
};

class WebVttParser {
 public:
  explicit WebVttParser(WebVttOptions options = {});
  WebVttParser(const WebVttParser&) = delete;
  WebVttParser& operator=(const WebVttParser&) = delete;
  ~WebVttParser();
  void Feed(std::string_view bytes, const std::function<void(TextCue)>& on_cue);
  [[nodiscard]] WebVttResult Finish();
  [[nodiscard]] static WebVttResult Parse(std::string_view bytes,
                                          WebVttOptions options = {});
  [[nodiscard]] static WebVttResult ParseWvttSample(std::string_view bytes,
                                                    double start_time,
                                                    double end_time,
                                                    WebVttOptions options = {});

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}

#endif
