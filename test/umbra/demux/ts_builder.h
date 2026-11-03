#ifndef TEST_UMBRA_DEMUX_TS_BUILDER_H_
#define TEST_UMBRA_DEMUX_TS_BUILDER_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "mp4_builder.h"

namespace umbra::demux::test {

struct EsSpec {
  uint8_t stream_type = 0;
  uint16_t pid = 0;
  Bytes descriptors;
};

struct PesOptions {
  std::optional<uint64_t> pts;
  std::optional<uint64_t> dts;
  uint8_t stream_id = 0xE0;
  bool bounded = false;
  bool random_access = false;
  std::optional<uint64_t> pcr;
  bool discontinuity = false;
};

class TsWriter {
 public:
  std::vector<Bytes> Pat(
      const std::vector<std::pair<uint16_t, uint16_t>>& programs,
      uint8_t version = 0);
  std::vector<Bytes> Pmt(uint16_t pmt_pid, uint16_t program, uint16_t pcr_pid,
                         const std::vector<EsSpec>& streams,
                         uint8_t version = 0, bool corrupt_crc = false);
  std::vector<Bytes> Pes(uint16_t pid, const Bytes& payload,
                         const PesOptions& options);

 private:
  std::vector<Bytes> Section(uint16_t pid, const Bytes& section);
  Bytes Packet(uint16_t pid, bool start, const Bytes& payload,
               const PesOptions& adaptation);

  std::array<uint8_t, 8192> continuity_{};
};

Bytes Flatten(const std::vector<Bytes>& packets);

Bytes H264AccessUnit(bool key, size_t filler, const Bytes& sei = {},
                     bool large_picture = false);
Bytes H264CaptionSei();
Bytes AdtsFrames(size_t count, size_t payload_bytes);
Bytes Ac3Frames(size_t count);
Bytes Id3Tag(size_t payload_bytes);

struct SyntheticSpec {
  uint64_t start = 900000;
  size_t frames = 50;
  size_t gop = 25;
  size_t video_bytes = 1500;
  bool audio = true;
  bool captions = false;
  bool id3 = false;
  bool bounded_video = false;
  size_t psi_every = 10;
  uint16_t pmt_pid = 0x100;
  uint16_t video_pid = 0x101;
  uint16_t audio_pid = 0x102;
  uint16_t id3_pid = 0x103;
};

struct Synthetic {
  std::vector<Bytes> packets;
  std::vector<size_t> psi_positions;
};

Synthetic BuildSynthetic(const SyntheticSpec& spec);

inline constexpr int64_t kFrameTicks = 3600;
inline constexpr int64_t kVideoDelayTicks = 7200;
inline constexpr size_t kAudioFramesPerPes = 3;
inline constexpr int64_t kAudioPesTicks = 5760;

}

#endif
