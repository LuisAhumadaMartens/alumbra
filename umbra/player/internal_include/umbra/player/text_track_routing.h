#ifndef UMBRA_PLAYER_INTERNAL_TEXT_TRACK_ROUTING_H_
#define UMBRA_PLAYER_INTERNAL_TEXT_TRACK_ROUTING_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/manifest/presentation.h"
#include "umbra/player/text_track.h"

namespace umbra::player::internal {

enum class TextFormat { kWebVtt, kSubRip, kAss, kTtml };

struct WebVttTimestampMap {
  double local_seconds = 0.0;
  std::uint64_t mpegts = 0;
};

struct TextTrackMetadata {
  TextTrackKind kind = TextTrackKind::kSubtitles;
  std::string label;
  std::string language;
};

[[nodiscard]] std::optional<TextFormat> SniffTextFormat(std::string_view bytes);
[[nodiscard]] std::optional<WebVttTimestampMap> ParseWebVttTimestampMap(
    std::string_view bytes);
[[nodiscard]] bool MapWebVttCues(std::vector<TextCue>& cues,
                                 std::optional<WebVttTimestampMap> map,
                                 double segment_start_seconds);
[[nodiscard]] bool LoadTextTrackPayload(TextTrack& track,
                                        std::string_view bytes,
                                        std::string fallback_encoding,
                                        bool auto_detect,
                                        double segment_start_seconds = 0.0);
[[nodiscard]] bool LoadTextTrackSample(TextTrack& track,
                                       const penumbra::StreamInfo& stream,
                                       const penumbra::EncodedSample& sample,
                                       double segment_start_seconds = 0.0);
[[nodiscard]] TextTrackMetadata MetadataForManifestTextTrack(
    const manifest::SwitchingSet& switching_set);
[[nodiscard]] TextTrackMetadata MetadataForHlsTextRendition(
    const manifest::HlsRendition& rendition);

}

#endif
