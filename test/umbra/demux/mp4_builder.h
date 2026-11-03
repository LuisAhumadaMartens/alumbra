#ifndef TEST_UMBRA_DEMUX_MP4_BUILDER_H_
#define TEST_UMBRA_DEMUX_MP4_BUILDER_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

namespace umbra::demux::test {

using Bytes = std::vector<uint8_t>;

class Writer {
 public:
  Writer& U8(uint32_t value);
  Writer& U16(uint32_t value);
  Writer& U32(uint32_t value);
  Writer& U64(uint64_t value);
  Writer& Zeros(size_t count);
  Writer& Text(std::string_view text);
  Writer& Add(const Bytes& bytes);

  [[nodiscard]] const Bytes& Data() const { return bytes_; }

 private:
  Bytes bytes_;
};

Bytes Box(std::string_view type, const Bytes& payload);
Bytes LargeBox(std::string_view type, const Bytes& payload);
Bytes FullBox(std::string_view type, uint8_t version, uint32_t flags,
              const Bytes& payload);
Bytes Concat(const std::vector<Bytes>& parts);
Bytes FromHex(std::string_view hex);

enum class TestCodec : uint8_t { kAvc, kAac, kUnknown };

struct SampleSpec {
  uint32_t size = 100;
  uint32_t duration = 1000;
  int32_t cts = 0;
  bool sync = true;
};

struct EditSpec {
  uint64_t segment_duration = 0;
  int64_t media_time = 0;
};

inline SampleSpec MakeSample(uint32_t size, uint32_t duration, int32_t cts,
                             bool sync) {
  return SampleSpec{
      .size = size, .duration = duration, .cts = cts, .sync = sync};
}

inline EditSpec MakeEdit(uint64_t segment_duration, int64_t media_time) {
  return EditSpec{.segment_duration = segment_duration,
                  .media_time = media_time};
}

struct TrackSpec {
  uint32_t id = 1;
  uint32_t timescale = 1000;
  TestCodec codec = TestCodec::kAac;
  std::vector<SampleSpec> samples;
  std::vector<EditSpec> edits;
  bool use_ctts = false;
  uint8_t ctts_version = 0;
  uint8_t elst_version = 0;
  uint32_t samples_per_chunk = 1;
  bool co64 = false;
  bool stz2 = false;
  std::map<size_t, Bytes> data_override;
};

struct ExpectedSample {
  uint32_t track_id = 0;
  size_t index = 0;
  uint64_t offset = 0;
  Bytes data;
};

struct Segment {
  uint64_t offset = 0;
  Bytes bytes;
};

struct BuiltFile {
  std::vector<Segment> segments;
  uint64_t size = 0;
  std::vector<ExpectedSample> expected;
  uint64_t mdat_payload_offset = 0;
  uint64_t moov_offset = 0;
  uint64_t moov_size = 0;

  [[nodiscard]] Bytes Flatten() const;
  [[nodiscard]] Bytes Read(uint64_t offset, size_t size) const;
};

struct ProgressiveSpec {
  std::vector<TrackSpec> tracks;
  bool moov_at_end = false;
  uint32_t movie_timescale = 1000;
  uint32_t movie_duration = 0;
  uint64_t data_shift = 0;
};

struct FragmentedSpec {
  std::vector<TrackSpec> tracks;
  size_t fragments = 3;
  size_t samples_per_fragment = 4;
  uint32_t movie_timescale = 1000;
  bool with_sidx = false;
  bool default_base_is_moof = true;
  bool with_tfdt = true;
  std::map<size_t, Bytes> boxes_before_fragment;
  std::map<size_t, int64_t> tfdt_override;
};

struct FragmentedFile {
  Bytes bytes;
  std::vector<uint64_t> fragment_offsets;
  std::vector<ExpectedSample> expected;
  uint64_t init_size = 0;
};

Bytes SampleBytes(uint32_t track_id, size_t index, uint32_t size);
Bytes AvcSampleEntryPayload();
Bytes Ftyp();
BuiltFile BuildProgressive(const ProgressiveSpec& spec);
FragmentedFile BuildFragmented(const FragmentedSpec& spec);
Bytes InitSegment(const FragmentedSpec& spec);
Bytes EmsgV0(std::string_view scheme, std::string_view value,
             uint32_t timescale, uint32_t delta, uint32_t duration, uint32_t id,
             const Bytes& message);
Bytes EmsgV1(std::string_view scheme, std::string_view value,
             uint32_t timescale, uint64_t time, uint32_t duration, uint32_t id,
             const Bytes& message);
Bytes PrftBox(uint32_t track_id, uint64_t ntp, uint64_t media_time,
              uint8_t version);

}

#endif
