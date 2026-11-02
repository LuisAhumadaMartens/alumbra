#ifndef UMBRA_DEMUX_MP4_TYPES_H_
#define UMBRA_DEMUX_MP4_TYPES_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "penumbra/interface/stream_info.h"
#include "umbra/demux/caption_extractor.h"

namespace umbra::demux {

constexpr uint32_t FourCc(char a, char b, char c, char d) {
  return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24) |
         (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8) |
         static_cast<uint32_t>(static_cast<uint8_t>(d));
}

inline constexpr size_t kMaxBoxDepth = 16;
inline constexpr uint64_t kMaxBufferedBoxBytes = 256ULL * 1024 * 1024;
inline constexpr uint64_t kMaxSamplesPerTrack = 4ULL * 1024 * 1024;
inline constexpr uint64_t kMaxSamplesPerMovie = 8ULL * 1024 * 1024;
inline constexpr size_t kMaxTracks = 32;
inline constexpr size_t kMaxSampleEntries = 64;
inline constexpr uint32_t kMaxSampleBytes = 256U * 1024 * 1024;

struct EditEntry {
  uint64_t segment_duration = 0;
  int64_t media_time = 0;
};

struct SttsEntry {
  uint32_t count = 0;
  uint32_t delta = 0;
};

struct CttsEntry {
  uint32_t count = 0;
  int32_t offset = 0;
};

struct StscEntry {
  uint32_t first_chunk = 0;
  uint32_t samples_per_chunk = 0;
  uint32_t description_index = 0;
};

struct RawSampleTable {
  std::vector<SttsEntry> stts;
  std::vector<CttsEntry> ctts;
  std::vector<StscEntry> stsc;
  std::vector<uint32_t> sizes;
  uint32_t constant_size = 0;
  uint32_t sample_count = 0;
  bool has_sizes = false;
  std::vector<uint64_t> chunk_offsets;
  std::vector<uint32_t> sync_samples;
  bool has_sync_table = false;
};

struct SampleRef {
  uint64_t offset = 0;
  uint32_t size = 0;
  uint32_t duration = 0;
  int64_t dts = 0;
  int32_t cts_offset = 0;
  uint16_t description = 0;
  bool sync = true;
};

using SampleList = std::vector<SampleRef>;

struct SampleEntryInfo {
  std::optional<penumbra::StreamInfo> stream;
  size_t nal_length_size = 4;
  std::optional<CaptionCodec> caption_codec;
};

struct TrackDefaults {
  uint32_t description_index = 1;
  uint32_t duration = 0;
  uint32_t size = 0;
  uint32_t flags = 0;
};

struct TrackDef {
  uint32_t track_id = 0;
  uint32_t timescale = 0;
  uint64_t duration = 0;
  uint32_t handler = 0;
  std::string language;
  std::vector<EditEntry> edits;
  std::vector<SampleEntryInfo> entries;
  RawSampleTable table;
  TrackDefaults defaults;
};

struct Movie {
  uint32_t movie_timescale = 0;
  uint64_t movie_duration = 0;
  uint64_t fragment_duration = 0;
  bool has_mvex = false;
  std::vector<TrackDef> tracks;
};

struct TrackTiming {
  int64_t shift = 0;
  std::optional<int64_t> end;
  size_t ignored_entries = 0;
};

}

#endif
