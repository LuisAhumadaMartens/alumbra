#ifndef TEST_UMBRA_DEMUX_MKV_BUILDER_H_
#define TEST_UMBRA_DEMUX_MKV_BUILDER_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mp4_builder.h"

namespace umbra::demux::test {

Bytes EbmlId(uint32_t id);
Bytes EbmlSize(uint64_t size);
Bytes Element(uint32_t id, const Bytes& payload);
Bytes UnknownSizeElement(uint32_t id, const Bytes& payload);
Bytes UintElement(uint32_t id, uint64_t value);
Bytes SintElement(uint32_t id, int64_t value);
Bytes FloatElement(uint32_t id, double value);
Bytes StringElement(uint32_t id, std::string_view value);

enum class MkvLacing : uint8_t { kNone, kXiph, kFixed, kEbml };

struct MkvBlockSpec {
  uint64_t track = 1;
  int16_t relative_ticks = 0;
  bool key = true;
  std::vector<Bytes> frames;
  MkvLacing lacing = MkvLacing::kNone;
  bool group = false;
  std::optional<uint64_t> duration;
  bool reference = false;
  std::optional<int64_t> discard_padding;
};

struct MkvClusterSpec {
  uint64_t timestamp = 0;
  std::vector<MkvBlockSpec> blocks;
  bool unknown_size = false;
  bool omit_timestamp = false;
  Bytes trailing_raw;
};

enum class MkvTestCodec : uint8_t {
  kVp8,
  kVp9,
  kAv1,
  kOpus,
  kAac,
  kFlac,
  kAc3,
  kMp3,
  kAss,
  kSsa,
  kVorbis,
  kTheora,
  kUnsupported,
};

struct MkvTrackSpec {
  uint64_t number = 1;
  MkvTestCodec codec = MkvTestCodec::kVp9;
  std::optional<uint64_t> default_duration_ns;
  uint64_t codec_delay_ns = 0;
  uint64_t seek_pre_roll_ns = 0;
  Bytes header_strip;
  bool encrypted = false;
  bool zlib = false;
  uint64_t width = 160;
  uint64_t height = 90;
  uint64_t channels = 2;
  Bytes codec_private;
};

struct MkvFileSpec {
  std::string doc_type = "webm";
  uint64_t timestamp_scale = 1000000;
  std::optional<double> duration;
  std::vector<MkvTrackSpec> tracks;
  std::vector<MkvClusterSpec> clusters;
  bool cues = false;
  bool cues_at_end = false;
  bool seek_head = true;
  bool unknown_segment_size = false;
  bool tracks_before_info = false;
  bool omit_header = false;
  bool omit_tracks = false;
  Bytes junk_before_clusters;
};

struct BuiltMkv {
  Bytes bytes;
  uint64_t segment_data_start = 0;
  std::vector<uint64_t> cluster_offsets;
  uint64_t cues_offset = 0;
  uint64_t first_cluster_offset = 0;
};

Bytes SimpleBlockElement(const MkvBlockSpec& block);
Bytes BlockGroupElement(const MkvBlockSpec& block);
Bytes ClusterElement(const MkvClusterSpec& cluster);
Bytes TracksElement(const std::vector<MkvTrackSpec>& tracks);
Bytes InfoElement(const MkvFileSpec& spec);
Bytes OpusHead(uint16_t pre_skip, uint8_t channels);
BuiltMkv BuildMkv(const MkvFileSpec& spec);
Bytes FrameBytes(uint64_t track, size_t index, size_t size);

}

#endif
