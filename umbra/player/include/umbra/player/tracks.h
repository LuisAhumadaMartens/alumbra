#ifndef UMBRA_PLAYER_TRACKS_H_
#define UMBRA_PLAYER_TRACKS_H_

#include <cstdint>
#include <string>

namespace umbra::player {

enum class AudioTrackKind : std::uint8_t {
  kMain,
  kAlternative,
  kDescriptions,
  kCommentary,
  kDub,
  kSign,
  kEmergency,
  kCaptions,
  kSubtitles,
  kMetadata
};

enum class VideoTrackKind : std::uint8_t {
  kMain,
  kAlternative,
  kDescriptions,
  kCommentary,
  kEmergency,
  kCaptions,
  kSubtitles,
  kSign,
  kMetadata
};

struct AudioTrack {
  std::string id;
  AudioTrackKind kind = AudioTrackKind::kMain;
  std::string label;
  std::string language;
  bool enabled = false;
};

struct VideoTrack {
  std::string id;
  VideoTrackKind kind = VideoTrackKind::kMain;
  std::string label;
  std::string language;
  bool selected = false;
  std::int32_t height = 0;
  std::int64_t bitrate_bps = 0;
  std::string codec;
};

}

#endif
