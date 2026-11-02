#include "umbra/codec/ac3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/status.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/reader_util.h"

namespace umbra::codec {
namespace {

constexpr std::array<std::int32_t, 3> kSampleRates{48000, 44100, 32000};
constexpr std::array<std::int32_t, 8> kAcmodChannels{2, 1, 2, 3, 3, 4, 4, 5};
constexpr std::size_t kMaxSubstreams = 8;
constexpr std::uint16_t kMaxDataRate = 0x1fff;
constexpr std::uint16_t kMaxChannelLocation = 0x1ff;

constexpr std::uint64_t kFl = 0x1;
constexpr std::uint64_t kFr = 0x2;
constexpr std::uint64_t kFc = 0x4;
constexpr std::uint64_t kLfe = 0x8;
constexpr std::uint64_t kBl = 0x10;
constexpr std::uint64_t kBr = 0x20;
constexpr std::uint64_t kFlc = 0x40;
constexpr std::uint64_t kFrc = 0x80;
constexpr std::uint64_t kBc = 0x100;
constexpr std::uint64_t kSl = 0x200;
constexpr std::uint64_t kSr = 0x400;
constexpr std::uint64_t kTc = 0x800;
constexpr std::uint64_t kTfl = 0x1000;
constexpr std::uint64_t kTfc = 0x2000;
constexpr std::uint64_t kTfr = 0x4000;

constexpr std::array<std::uint64_t, 8> kAcmodMasks{kFl | kFr,
                                                   kFc,
                                                   kFl | kFr,
                                                   kFl | kFr | kFc,
                                                   kFl | kFr | kBc,
                                                   kFl | kFr | kFc | kBc,
                                                   kFl | kFr | kSl | kSr,
                                                   kFl | kFr | kFc | kSl | kSr};

struct DependentGroup {
  std::int32_t channels;
  std::uint64_t mask;
};

constexpr std::array<DependentGroup, 9> kDependentGroups{{
    {.channels = 2, .mask = kFlc | kFrc},
    {.channels = 2, .mask = kBl | kBr},
    {.channels = 1, .mask = kBc},
    {.channels = 1, .mask = kTc},
    {.channels = 2, .mask = 0},
    {.channels = 2, .mask = 0},
    {.channels = 2, .mask = kTfl | kTfr},
    {.channels = 1, .mask = kTfc},
    {.channels = 1, .mask = 0},
}};

void CheckRate(BitstreamReader& bits, std::uint8_t fscod) {
  if (fscod >= kSampleRates.size()) {
    bits.Fail("reserved sample rate code");
  }
}

Eac3Substream ReadSubstream(BitstreamReader& bits) {
  Eac3Substream substream;
  substream.fscod = static_cast<std::uint8_t>(bits.Bits(2));
  substream.bsid = static_cast<std::uint8_t>(bits.Bits(5));
  bits.Skip(1);
  substream.asvc = bits.Flag();
  substream.bsmod = static_cast<std::uint8_t>(bits.Bits(3));
  substream.acmod = static_cast<std::uint8_t>(bits.Bits(3));
  substream.lfeon = bits.Flag();
  bits.Skip(3);
  substream.num_dependent_substreams = static_cast<std::uint8_t>(bits.Bits(4));
  if (substream.num_dependent_substreams > 0) {
    substream.chan_loc = static_cast<std::uint16_t>(bits.Bits(9));
  } else {
    bits.Skip(1);
  }
  CheckRate(bits, substream.fscod);
  return substream;
}

std::int32_t ChannelsOf(const Eac3Substream& substream) {
  std::int32_t channels =
      kAcmodChannels.at(substream.acmod) + (substream.lfeon ? 1 : 0);
  for (std::size_t i = 0; i < kDependentGroups.size(); ++i) {
    if (((substream.chan_loc >> i) & 1U) != 0) {
      channels += kDependentGroups.at(i).channels;
    }
  }
  return channels;
}

std::uint64_t MaskOf(const Eac3Substream& substream) {
  std::uint64_t mask =
      kAcmodMasks.at(substream.acmod) | (substream.lfeon ? kLfe : 0);
  for (std::size_t i = 0; i < kDependentGroups.size(); ++i) {
    if (((substream.chan_loc >> i) & 1U) != 0) {
      mask |= kDependentGroups.at(i).mask;
    }
  }
  return mask;
}

penumbra::AudioInfo InfoFor(const Eac3Substream& substream) {
  penumbra::AudioInfo info;
  info.sample_rate = kSampleRates.at(substream.fscod);
  info.channel_count = ChannelsOf(substream);
  info.channel_mask = MaskOf(substream);
  info.sample_format = penumbra::SampleFormat::kF32Planar;
  return info;
}

bool SubstreamInRange(const Eac3Substream& s) {
  return s.fscod <= 3 && s.bsid <= 31 && s.bsmod <= 7 && s.acmod <= 7 &&
         s.num_dependent_substreams <= 15 && s.chan_loc <= kMaxChannelLocation;
}

void AppendSubstream(const Eac3Substream& s, std::vector<std::uint8_t>& out) {
  std::uint32_t packed =
      (static_cast<std::uint32_t>(s.fscod) << 30U) |
      (static_cast<std::uint32_t>(s.bsid) << 25U) | (s.asvc ? 1U << 23U : 0U) |
      (static_cast<std::uint32_t>(s.bsmod) << 20U) |
      (static_cast<std::uint32_t>(s.acmod) << 17U) |
      (s.lfeon ? 1U << 16U : 0U) |
      (static_cast<std::uint32_t>(s.num_dependent_substreams) << 9U);
  if (s.num_dependent_substreams > 0) {
    packed |= s.chan_loc;
  }
  out.push_back(static_cast<std::uint8_t>(packed >> 24U));
  out.push_back(static_cast<std::uint8_t>(packed >> 16U));
  out.push_back(static_cast<std::uint8_t>(packed >> 8U));
  if (s.num_dependent_substreams > 0) {
    out.push_back(static_cast<std::uint8_t>(packed));
  }
}

}

absl::StatusOr<Ac3Config> ParseDac3(Bytes data) {
  BitstreamReader bits(data, "dac3");
  Ac3Config config;
  config.fscod = static_cast<std::uint8_t>(bits.Bits(2));
  config.bsid = static_cast<std::uint8_t>(bits.Bits(5));
  config.bsmod = static_cast<std::uint8_t>(bits.Bits(3));
  config.acmod = static_cast<std::uint8_t>(bits.Bits(3));
  config.lfeon = bits.Flag();
  config.bit_rate_code = static_cast<std::uint8_t>(bits.Bits(5));
  bits.Skip(5);
  CheckRate(bits, config.fscod);
  ALUMBRA_RETURN_IF_ERROR(bits.FirstError());
  return config;
}

absl::StatusOr<Eac3Config> ParseDec3(Bytes data) {
  BitstreamReader bits(data, "dec3");
  Eac3Config config;
  config.data_rate = static_cast<std::uint16_t>(bits.Bits(13));
  const std::uint32_t count = bits.Bits(3);
  for (std::uint32_t i = 0; i <= count && bits.Ok(); ++i) {
    config.substreams.push_back(ReadSubstream(bits));
  }
  if (bits.Ok() && bits.remaining() >= 16) {
    bits.Skip(7);
    config.joc = bits.Flag();
    config.joc_complexity_index = static_cast<std::uint8_t>(bits.Bits(8));
  }
  ALUMBRA_RETURN_IF_ERROR(bits.FirstError());
  return config;
}

absl::StatusOr<std::vector<std::uint8_t>> SerializeDac3(
    const Ac3Config& config) {
  if (config.fscod > 3 || config.bsid > 31 || config.bsmod > 7 ||
      config.acmod > 7 || config.bit_rate_code > 31) {
    return absl::InvalidArgumentError("dac3 field is out of range");
  }
  const std::uint32_t packed =
      (static_cast<std::uint32_t>(config.fscod) << 22U) |
      (static_cast<std::uint32_t>(config.bsid) << 17U) |
      (static_cast<std::uint32_t>(config.bsmod) << 14U) |
      (static_cast<std::uint32_t>(config.acmod) << 11U) |
      (config.lfeon ? 1U << 10U : 0U) |
      (static_cast<std::uint32_t>(config.bit_rate_code) << 5U);
  return std::vector<std::uint8_t>{static_cast<std::uint8_t>(packed >> 16U),
                                   static_cast<std::uint8_t>(packed >> 8U),
                                   static_cast<std::uint8_t>(packed)};
}

absl::StatusOr<std::vector<std::uint8_t>> SerializeDec3(
    const Eac3Config& config) {
  if (config.substreams.empty() || config.substreams.size() > kMaxSubstreams ||
      config.data_rate > kMaxDataRate) {
    return absl::InvalidArgumentError("dec3 field is out of range");
  }
  const std::uint32_t head =
      (static_cast<std::uint32_t>(config.data_rate) << 3U) |
      static_cast<std::uint32_t>(config.substreams.size() - 1);
  std::vector<std::uint8_t> output{static_cast<std::uint8_t>(head >> 8U),
                                   static_cast<std::uint8_t>(head)};
  for (const Eac3Substream& substream : config.substreams) {
    if (!SubstreamInRange(substream)) {
      return absl::InvalidArgumentError("dec3 substream field is out of range");
    }
    AppendSubstream(substream, output);
  }
  if (config.joc) {
    output.push_back(1);
    output.push_back(config.joc_complexity_index);
  }
  return output;
}

Ac3Codec ToCodec([[maybe_unused]] const Ac3Config& config) { return {}; }

Eac3Codec ToCodec([[maybe_unused]] const Eac3Config& config) { return {}; }

penumbra::AudioInfo ToAudioInfo(const Ac3Config& config) {
  Eac3Substream substream;
  substream.fscod = config.fscod;
  substream.acmod = config.acmod;
  substream.lfeon = config.lfeon;
  return InfoFor(substream);
}

penumbra::AudioInfo ToAudioInfo(const Eac3Config& config) {
  return InfoFor(config.substreams.front());
}

}
