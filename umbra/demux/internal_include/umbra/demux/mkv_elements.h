#ifndef UMBRA_DEMUX_MKV_ELEMENTS_H_
#define UMBRA_DEMUX_MKV_ELEMENTS_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "umbra/demux/ebml.h"

namespace umbra::demux {

inline constexpr size_t kMaxMkvTracks = 32;
inline constexpr size_t kMaxCuePoints = size_t{1} << 20;
inline constexpr size_t kMaxScanPoints = size_t{1} << 16;
inline constexpr uint64_t kMaxMkvTimeline = uint64_t{1} << 61;
inline constexpr uint64_t kMaxHeaderElementBytes = uint64_t{1} << 16;
inline constexpr uint64_t kMaxSeekHeadBytes = uint64_t{1} << 20;
inline constexpr uint64_t kMaxInfoBytes = uint64_t{1} << 20;
inline constexpr uint64_t kMaxTracksBytes = uint64_t{16} << 20;
inline constexpr uint64_t kMaxCuesBytes = uint64_t{32} << 20;
inline constexpr uint64_t kMaxBlockElementBytes = (uint64_t{256} << 20) + 1024;

inline constexpr uint64_t kTrackTypeVideo = 1;
inline constexpr uint64_t kTrackTypeAudio = 2;

struct MkvColour {
  std::optional<uint64_t> primaries;
  std::optional<uint64_t> transfer;
  std::optional<uint64_t> matrix;
  std::optional<uint64_t> range;
};

struct MkvVideo {
  uint64_t pixel_width = 0;
  uint64_t pixel_height = 0;
  std::optional<uint64_t> display_width;
  std::optional<uint64_t> display_height;
  uint64_t display_unit = 0;
  std::optional<MkvColour> colour;
};

struct MkvAudio {
  double sampling_frequency = 8000.0;
  uint64_t channels = 1;
};

struct MkvTrack {
  uint64_t number = 0;
  uint64_t type = 0;
  std::string codec_id;
  std::vector<uint8_t> codec_private;
  std::optional<uint64_t> default_duration_ns;
  uint64_t codec_delay_ns = 0;
  uint64_t seek_pre_roll_ns = 0;
  std::string language = "eng";
  bool has_language_override = false;
  MkvVideo video;
  MkvAudio audio;
  std::vector<uint8_t> header_strip;
  bool unsupported_encoding = false;
  bool encrypted = false;
};

struct MkvInfo {
  uint64_t timestamp_scale = 1000000;
  std::optional<double> duration_ticks;
};

struct MkvSeekHead {
  std::optional<uint64_t> cues_position;
};

struct MkvCuePoint {
  uint64_t time = 0;
  uint64_t track = 0;
  uint64_t cluster_position = 0;
};

struct MkvBlockGroup {
  size_t block_offset = 0;
  size_t block_size = 0;
  std::optional<uint64_t> duration;
  bool has_reference = false;
  std::optional<int64_t> discard_padding_ns;
};

[[nodiscard]] absl::Status ParseEbmlHeaderElement(EbmlBytes payload);
[[nodiscard]] absl::StatusOr<MkvSeekHead> ParseSeekHead(EbmlBytes payload);
[[nodiscard]] absl::StatusOr<MkvInfo> ParseInfo(EbmlBytes payload);
[[nodiscard]] absl::StatusOr<std::vector<MkvTrack>> ParseTracks(
    EbmlBytes payload);
[[nodiscard]] absl::StatusOr<std::vector<MkvCuePoint>> ParseCues(
    EbmlBytes payload);
[[nodiscard]] absl::StatusOr<MkvBlockGroup> ParseBlockGroup(EbmlBytes payload);

}

#endif
