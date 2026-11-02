#include "umbra/codec/aac.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/status.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/reader_util.h"

namespace umbra::codec {
namespace {

constexpr std::uint32_t kEscapeObjectType = 31;
constexpr std::uint32_t kExplicitFrequencyIndex = 15;
constexpr std::uint32_t kSbrSyncExtension = 0x2b7;
constexpr std::uint32_t kPsSyncExtension = 0x548;
constexpr std::size_t kSyncExtensionBits = 16;
constexpr std::size_t kPsExtensionBits = 12;
constexpr std::uint8_t kMpeg4AudioOti = 0x40;
constexpr std::uint8_t kAacLayered = 6;
constexpr std::uint8_t kErAacScalable = 20;
constexpr std::uint8_t kErAacLd = 23;
constexpr std::uint8_t kErAacLc = 17;
constexpr std::uint8_t kErAacLtp = 19;
constexpr std::uint8_t kErBsac = 22;

constexpr std::array<std::int32_t, 13> kSamplingFrequencies{
    96000, 88200, 64000, 48000, 44100, 32000, 24000,
    22050, 16000, 12000, 11025, 8000,  7350};

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

struct ChannelLayout {
  std::uint8_t configuration;
  std::int32_t count;
  std::uint64_t mask;
};

constexpr std::array<ChannelLayout, 11> kLayouts{{
    {.configuration = 1, .count = 1, .mask = kFc},
    {.configuration = 2, .count = 2, .mask = kFl | kFr},
    {.configuration = 3, .count = 3, .mask = kFc | kFl | kFr},
    {.configuration = 4, .count = 4, .mask = kFc | kFl | kFr | kBc},
    {.configuration = 5, .count = 5, .mask = kFc | kFl | kFr | kSl | kSr},
    {.configuration = 6,
     .count = 6,
     .mask = kFc | kFl | kFr | kSl | kSr | kLfe},
    {.configuration = 7,
     .count = 8,
     .mask = kFc | kFlc | kFrc | kFl | kFr | kSl | kSr | kLfe},
    {.configuration = 11,
     .count = 7,
     .mask = kFc | kFl | kFr | kSl | kSr | kBc | kLfe},
    {.configuration = 12,
     .count = 8,
     .mask = kFc | kFl | kFr | kSl | kSr | kBl | kBr | kLfe},
    {.configuration = 13, .count = 24, .mask = 0},
    {.configuration = 14, .count = 8, .mask = 0},
}};

const ChannelLayout* FindLayout(std::uint8_t configuration) {
  for (const ChannelLayout& layout : kLayouts) {
    if (layout.configuration == configuration) {
      return &layout;
    }
  }
  return nullptr;
}

std::uint8_t ReadObjectType(BitstreamReader& bits) {
  std::uint32_t type = bits.Bits(5);
  if (type == kEscapeObjectType) {
    type = 32 + bits.Bits(6);
  }
  return static_cast<std::uint8_t>(type);
}

std::int32_t ReadFrequency(BitstreamReader& bits) {
  const std::uint32_t index = bits.Bits(4);
  if (index == kExplicitFrequencyIndex) {
    const std::uint32_t frequency = bits.Bits(24);
    if (frequency == 0) {
      bits.Fail("zero sampling frequency");
    }
    return static_cast<std::int32_t>(frequency);
  }
  if (index >= kSamplingFrequencies.size()) {
    bits.Fail("reserved sampling frequency");
    return 1;
  }
  return kSamplingFrequencies.at(index);
}

std::int32_t ReadElementList(BitstreamReader& bits, std::uint32_t count) {
  std::int32_t channels = 0;
  for (std::uint32_t i = 0; i < count; ++i) {
    channels += bits.Flag() ? 2 : 1;
    bits.Skip(4);
  }
  return channels;
}

std::int32_t ReadProgramConfigElement(BitstreamReader& bits) {
  bits.Skip(4 + 2 + 4);
  const std::uint32_t front = bits.Bits(4);
  const std::uint32_t side = bits.Bits(4);
  const std::uint32_t back = bits.Bits(4);
  const std::uint32_t lfe = bits.Bits(2);
  const std::uint32_t data = bits.Bits(3);
  const std::uint32_t coupling = bits.Bits(4);
  if (bits.Flag()) {
    bits.Skip(4);
  }
  if (bits.Flag()) {
    bits.Skip(4);
  }
  if (bits.Flag()) {
    bits.Skip(3);
  }
  const std::int32_t channels = ReadElementList(bits, front) +
                                ReadElementList(bits, side) +
                                ReadElementList(bits, back);
  bits.Skip((4 * static_cast<std::size_t>(lfe)) +
            (4 * static_cast<std::size_t>(data)) +
            (5 * static_cast<std::size_t>(coupling)));
  bits.AlignToByte();
  bits.Skip(8 * static_cast<std::size_t>(bits.Bits(8)));
  return channels + static_cast<std::int32_t>(lfe);
}

bool IsErrorResilient(std::uint8_t type) {
  return type == kErAacLc || (type >= kErAacLtp && type <= 27) || type == 39;
}

bool UsesGaSpecificConfig(std::uint8_t type) {
  return (type >= 1 && type <= 4) || type == kAacLayered || type == 7 ||
         type == kErAacLc || (type >= kErAacLtp && type <= kErAacLd);
}

void ReadGaSpecificConfig(BitstreamReader& bits, AacConfig& config) {
  config.frame_length_960 = bits.Flag();
  if (bits.Flag()) {
    bits.Skip(14);
  }
  const bool extension = bits.Flag();
  if (config.channel_configuration == 0) {
    config.channel_count = ReadProgramConfigElement(bits);
  }
  const std::uint8_t type = config.audio_object_type;
  if (type == kAacLayered || type == kErAacScalable) {
    bits.Skip(3);
  }
  if (!extension) {
    return;
  }
  if (type == kErBsac) {
    bits.Skip(16);
  }
  if (type == kErAacLc || type == kErAacLtp || type == kErAacScalable ||
      type == kErAacLd) {
    bits.Skip(3);
  }
  bits.Skip(1);
}

void ReadSbrSignalling(BitstreamReader& bits, AacConfig& config) {
  config.sbr = bits.Flag();
  if (!config.sbr) {
    return;
  }
  config.explicit_extension = true;
  config.extension_sampling_frequency = ReadFrequency(bits);
  if (bits.remaining() >= kPsExtensionBits &&
      bits.Bits(11) == kPsSyncExtension) {
    config.ps = bits.Flag();
  }
}

void ReadBackwardCompatibleExtension(BitstreamReader& bits, AacConfig& config) {
  if (bits.remaining() < kSyncExtensionBits ||
      bits.Bits(11) != kSbrSyncExtension) {
    return;
  }
  if (ReadObjectType(bits) == kAacSbr) {
    ReadSbrSignalling(bits, config);
  }
}

void ReadExplicitExtension(BitstreamReader& bits, AacConfig& config) {
  config.sbr = true;
  config.explicit_extension = true;
  config.ps = config.audio_object_type == kAacPs;
  config.extension_sampling_frequency = ReadFrequency(bits);
  config.audio_object_type = ReadObjectType(bits);
  if (config.audio_object_type == kErBsac) {
    bits.Skip(4);
  }
}

absl::Status ReadChannelConfiguration(BitstreamReader& bits,
                                      AacConfig& config) {
  config.channel_configuration = static_cast<std::uint8_t>(bits.Bits(4));
  if (config.channel_configuration == 0) {
    return absl::OkStatus();
  }
  const ChannelLayout* layout = FindLayout(config.channel_configuration);
  if (layout == nullptr) {
    return Corrupt(
        absl::StrCat("AudioSpecificConfig channel configuration ",
                     static_cast<int>(config.channel_configuration)));
  }
  config.channel_count = layout->count;
  config.channel_mask = layout->mask;
  return absl::OkStatus();
}

}

absl::StatusOr<AacConfig> ParseAudioSpecificConfig(Bytes data) {
  BitstreamReader bits(data, "AudioSpecificConfig");
  AacConfig config;
  config.audio_object_type = ReadObjectType(bits);
  config.sampling_frequency = ReadFrequency(bits);
  ALUMBRA_RETURN_IF_ERROR(bits.FirstError());
  ALUMBRA_RETURN_IF_ERROR(ReadChannelConfiguration(bits, config));
  if (config.audio_object_type == kAacSbr ||
      config.audio_object_type == kAacPs) {
    ReadExplicitExtension(bits, config);
  }
  ALUMBRA_RETURN_IF_ERROR(bits.FirstError());
  if (!UsesGaSpecificConfig(config.audio_object_type)) {
    return Unsupported(absl::StrCat(
        "AAC audio object type ", static_cast<int>(config.audio_object_type)));
  }
  ReadGaSpecificConfig(bits, config);
  if (IsErrorResilient(config.audio_object_type)) {
    bits.Skip(2);
  }
  if (!config.explicit_extension) {
    ReadBackwardCompatibleExtension(bits, config);
  }
  ALUMBRA_RETURN_IF_ERROR(bits.FirstError());
  if (config.channel_count <= 0) {
    return Corrupt("AudioSpecificConfig describes no channels");
  }
  return config;
}

Mp4aCodec ToCodec(const AacConfig& config) {
  return {.object_type_indication = kMpeg4AudioOti,
          .audio_object_type = config.OutputObjectType()};
}

penumbra::AudioInfo ToAudioInfo(const AacConfig& config) {
  penumbra::AudioInfo info;
  info.sample_rate = config.OutputSampleRate();
  info.channel_count = config.OutputChannelCount();
  info.channel_mask = config.ps ? (kFl | kFr) : config.channel_mask;
  info.sample_format = penumbra::SampleFormat::kF32Planar;
  return info;
}

}
