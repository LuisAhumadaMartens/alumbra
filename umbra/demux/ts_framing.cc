#include "umbra/demux/ts_framing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/status.h"
#include "umbra/codec/aac.h"
#include "umbra/codec/ac3.h"
#include "umbra/codec/avc.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/hevc.h"
#include "umbra/codec/nal_unit.h"
#include "umbra/codec/reader_util.h"
#include "umbra/demux/caption_extractor.h"

namespace umbra::demux::ts {
namespace {

constexpr size_t kAdtsMinHeader = 7;
constexpr size_t kAdtsCrcHeader = 9;
constexpr uint32_t kAacSamplesPerBlock = 1024;
constexpr uint32_t kAc3SamplesPerFrame = 1536;
constexpr std::array<int32_t, 13> kAdtsRates = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000,
    22050, 16000, 12000, 11025, 8000,  7350};
constexpr std::array<int32_t, 3> kAc3Rates = {48000, 44100, 32000};
constexpr std::array<int32_t, 3> kEac3ReducedRates = {24000, 22050, 16000};
constexpr std::array<uint32_t, 4> kEac3Blocks = {1, 2, 3, 6};
constexpr std::array<uint16_t, 19> kAc3Bitrates = {
    32,  40,  48,  56,  64,  80,  96,  112, 128, 160,
    192, 224, 256, 320, 384, 448, 512, 576, 640};
constexpr std::array<uint16_t, 15> kMpeg1Layer1 = {
    0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448};
constexpr std::array<uint16_t, 15> kMpeg1Layer2 = {
    0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384};
constexpr std::array<uint16_t, 15> kMpeg1Layer3 = {
    0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
constexpr std::array<uint16_t, 15> kMpeg2Layer1 = {
    0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256};
constexpr std::array<uint16_t, 15> kMpeg2Layer23 = {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
constexpr uint8_t kMp3ObjectType = 0x6B;
constexpr uint8_t kMp2ObjectType = 0x69;
constexpr uint8_t kAc3MaxBsid = 10;

struct ParsedFrame {
  bool valid = false;
  AudioFrame frame;
};

ParsedFrame ParseAdts(std::span<const uint8_t> data) {
  ParsedFrame result;
  if (data.size() < kAdtsMinHeader || data[0] != 0xFF ||
      (data[1] & 0xF6U) != 0xF0U) {
    return result;
  }
  const size_t header = (data[1] & 1U) != 0 ? kAdtsMinHeader : kAdtsCrcHeader;
  const size_t rate_index = (data[2] >> 2U) & 0x0FU;
  const size_t size = (static_cast<size_t>(data[3] & 3U) << 11U) |
                      (static_cast<size_t>(data[4]) << 3U) | (data[5] >> 5U);
  if (rate_index >= kAdtsRates.size() || size < header) {
    return result;
  }
  result.valid = true;
  result.frame.size = size;
  result.frame.samples = kAacSamplesPerBlock * ((data[6] & 3U) + 1U);
  result.frame.sample_rate = kAdtsRates.at(rate_index);
  return result;
}

struct MpegAudioHeader {
  bool valid = false;
  bool mpeg1 = false;
  uint32_t layer = 0;
  int32_t sample_rate = 0;
  uint32_t samples = 0;
  size_t size = 0;
  int32_t channels = 2;
};

const std::array<uint16_t, 15>& BitrateTable(bool mpeg1, uint32_t layer) {
  if (!mpeg1) {
    return layer == 1 ? kMpeg2Layer1 : kMpeg2Layer23;
  }
  if (layer == 1) {
    return kMpeg1Layer1;
  }
  return layer == 2 ? kMpeg1Layer2 : kMpeg1Layer3;
}

MpegAudioHeader ParseMpegAudio(std::span<const uint8_t> data) {
  MpegAudioHeader header;
  if (data.size() < 4 || data[0] != 0xFF || (data[1] & 0xE0U) != 0xE0U) {
    return header;
  }
  const uint32_t version = (data[1] >> 3U) & 3U;
  const uint32_t layer = 4U - ((data[1] >> 1U) & 3U);
  const size_t bitrate_index = data[2] >> 4U;
  const size_t rate_index = (data[2] >> 2U) & 3U;
  if (version == 1 || layer == 4 || bitrate_index == 0 || bitrate_index == 15 ||
      rate_index == 3) {
    return header;
  }
  const bool mpeg1 = version == 3;
  const std::array<uint16_t, 15>& table = BitrateTable(mpeg1, layer);
  int32_t rate = std::array<int32_t, 3>{44100, 48000, 32000}.at(rate_index);
  if (version == 2) {
    rate /= 2;
  } else if (version == 0) {
    rate /= 4;
  }
  const auto bitrate = static_cast<size_t>(table.at(bitrate_index)) * 1000U;
  const auto divisor = static_cast<size_t>(rate);
  const size_t padding = (data[2] >> 1U) & 1U;
  header.mpeg1 = mpeg1;
  header.layer = layer;
  header.sample_rate = rate;
  header.channels = (data[3] >> 6U) == 3 ? 1 : 2;
  if (layer == 1) {
    header.samples = 384;
    header.size = (((12U * bitrate) / divisor) + padding) * 4U;
  } else if (layer == 2 || mpeg1) {
    header.samples = 1152;
    header.size = ((144U * bitrate) / divisor) + padding;
  } else {
    header.samples = 576;
    header.size = ((72U * bitrate) / divisor) + padding;
  }
  header.valid = header.size >= 4;
  return header;
}

ParsedFrame ParseMpegAudioFrame(std::span<const uint8_t> data) {
  ParsedFrame result;
  const MpegAudioHeader header = ParseMpegAudio(data);
  if (!header.valid) {
    return result;
  }
  result.valid = true;
  result.frame.size = header.size;
  result.frame.samples = header.samples;
  result.frame.sample_rate = header.sample_rate;
  return result;
}

ParsedFrame ParseAc3(std::span<const uint8_t> data) {
  ParsedFrame result;
  if (data.size() < 8 || data[0] != 0x0B || data[1] != 0x77 ||
      (data[5] >> 3U) > kAc3MaxBsid) {
    return result;
  }
  const size_t rate_index = data[4] >> 6U;
  const size_t code = data[4] & 0x3FU;
  if (rate_index > 2 || (code >> 1U) >= kAc3Bitrates.size()) {
    return result;
  }
  const uint32_t bitrate = kAc3Bitrates.at(code >> 1U);
  uint32_t words = 0;
  if (rate_index == 0) {
    words = bitrate * 2;
  } else if (rate_index == 2) {
    words = bitrate * 3;
  } else {
    words = ((bitrate * 1536000U) / 705600U) + (code & 1U);
  }
  result.valid = true;
  result.frame.size = static_cast<size_t>(words) * 2;
  result.frame.samples = kAc3SamplesPerFrame;
  result.frame.sample_rate = kAc3Rates.at(rate_index);
  return result;
}

ParsedFrame ParseEac3(std::span<const uint8_t> data) {
  ParsedFrame result;
  if (data.size() < 8 || data[0] != 0x0B || data[1] != 0x77 ||
      (data[5] >> 3U) <= kAc3MaxBsid) {
    return result;
  }
  const uint32_t stream_type = data[2] >> 6U;
  if (stream_type == 3) {
    return result;
  }
  const size_t size =
      (((static_cast<size_t>(data[2] & 7U) << 8U) | data[3]) * 2) + 2;
  const size_t rate_index = data[4] >> 6U;
  uint32_t blocks = 6;
  int32_t rate = 0;
  if (rate_index == 3) {
    const size_t reduced = (data[4] >> 4U) & 3U;
    if (reduced == 3) {
      return result;
    }
    rate = kEac3ReducedRates.at(reduced);
  } else {
    rate = kAc3Rates.at(rate_index);
    blocks = kEac3Blocks.at((data[4] >> 4U) & 3U);
  }
  if (size < 8) {
    return result;
  }
  result.valid = true;
  result.frame.size = size;
  result.frame.samples = blocks * 256;
  result.frame.sample_rate = rate;
  result.frame.dependent = stream_type == 1;
  return result;
}

template <typename Parser>
AudioSplit SplitWith(std::span<const uint8_t> payload, const Parser& parse) {
  AudioSplit split;
  size_t position = 0;
  while (position < payload.size()) {
    ParsedFrame parsed = parse(payload.subspan(position));
    if (!parsed.valid) {
      split.lost_sync = true;
      ++position;
      continue;
    }
    if (parsed.frame.size > payload.size() - position) {
      split.lost_sync = true;
      break;
    }
    parsed.frame.offset = position;
    position += parsed.frame.size;
    split.frames.push_back(parsed.frame);
  }
  return split;
}

AudioSplit MergeDependents(const AudioSplit& split) {
  AudioSplit merged;
  merged.lost_sync = split.lost_sync;
  for (const AudioFrame& frame : split.frames) {
    if (frame.dependent) {
      if (!merged.frames.empty() &&
          merged.frames.back().offset + merged.frames.back().size ==
              frame.offset) {
        merged.frames.back().size += frame.size;
        ++merged.frames.back().dependents;
      } else {
        merged.lost_sync = true;
      }
      continue;
    }
    merged.frames.push_back(frame);
  }
  return merged;
}

absl::StatusOr<penumbra::StreamInfo> BuildAdtsInfo(
    std::span<const uint8_t> frame, uint32_t track_id,
    const std::string& language) {
  if (frame.size() < kAdtsMinHeader) {
    return codec::Corrupt("ADTS frame is shorter than its header");
  }
  const uint32_t object_type = ((frame[2] >> 6U) & 3U) + 1U;
  const uint32_t rate_index = (frame[2] >> 2U) & 0x0FU;
  const uint32_t channels = ((frame[2] & 1U) << 2U) | (frame[3] >> 6U);
  const std::array<uint8_t, 2> specific = {
      static_cast<uint8_t>((object_type << 3U) | (rate_index >> 1U)),
      static_cast<uint8_t>(((rate_index & 1U) << 7U) | (channels << 3U))};
  ALUMBRA_ASSIGN_OR_RETURN(const codec::AacConfig config,
                           codec::ParseAudioSpecificConfig(specific));
  penumbra::StreamInfo info;
  info.track_id = track_id;
  const codec::CodecDescription description = codec::ToCodec(config);
  info.codec = codec::CodecIdOf(description);
  info.codec_string = codec::FormatCodecString(description);
  info.codec_config = penumbra::SharedBytes::Copy(specific);
  info.aac_framing = penumbra::AacFraming::kAdts;
  info.time_base = kTimeBase;
  info.language = language;
  info.format = codec::ToAudioInfo(config);
  return info;
}

absl::StatusOr<penumbra::StreamInfo> BuildMpegAudioInfo(
    std::span<const uint8_t> frame, uint32_t track_id,
    const std::string& language) {
  const MpegAudioHeader header = ParseMpegAudio(frame);
  if (!header.valid) {
    return codec::Corrupt("MPEG audio frame has an invalid header");
  }
  codec::Mp4aCodec description;
  description.object_type_indication =
      header.mpeg1 ? kMp3ObjectType : kMp2ObjectType;
  penumbra::StreamInfo info;
  info.track_id = track_id;
  info.codec = codec::CodecIdOf(description);
  info.codec_string = codec::FormatCodecString(description);
  info.time_base = kTimeBase;
  info.language = language;
  penumbra::AudioInfo audio;
  audio.sample_rate = header.sample_rate;
  audio.channel_count = header.channels;
  info.format = audio;
  return info;
}

absl::StatusOr<penumbra::StreamInfo> BuildAc3Info(
    std::span<const uint8_t> frame, uint32_t track_id,
    const std::string& language) {
  const ParsedFrame parsed = ParseAc3(frame);
  if (!parsed.valid) {
    return codec::Corrupt("AC-3 frame has an invalid header");
  }
  codec::Ac3Config config;
  config.fscod = frame[4] >> 6U;
  config.bit_rate_code = static_cast<uint8_t>((frame[4] & 0x3FU) >> 1U);
  config.bsid = frame[5] >> 3U;
  config.bsmod = frame[5] & 7U;
  codec::BitstreamReader bits(frame.subspan(6), "AC-3 bit stream information");
  config.acmod = static_cast<uint8_t>(bits.Bits(3));
  if ((config.acmod & 1U) != 0 && config.acmod != 1) {
    bits.Skip(2);
  }
  if ((config.acmod & 4U) != 0) {
    bits.Skip(2);
  }
  if (config.acmod == 2) {
    bits.Skip(2);
  }
  config.lfeon = bits.Flag();
  if (!bits.Ok()) {
    return bits.FirstError();
  }
  ALUMBRA_ASSIGN_OR_RETURN(const std::vector<uint8_t> record,
                           codec::SerializeDac3(config));
  const codec::CodecDescription description = codec::ToCodec(config);
  penumbra::StreamInfo info;
  info.track_id = track_id;
  info.codec = codec::CodecIdOf(description);
  info.codec_string = codec::FormatCodecString(description);
  info.codec_config = penumbra::SharedBytes::Copy(record);
  info.time_base = kTimeBase;
  info.language = language;
  info.format = codec::ToAudioInfo(config);
  return info;
}

absl::StatusOr<penumbra::StreamInfo> BuildEac3Info(
    std::span<const uint8_t> frame, uint32_t track_id,
    const std::string& language) {
  const AudioSplit split = MergeDependents(SplitWith(frame, ParseEac3));
  if (split.frames.empty()) {
    return codec::Corrupt("E-AC-3 frame has an invalid header");
  }
  const AudioFrame& first = split.frames.front();
  codec::Eac3Substream substream;
  substream.fscod = frame[4] >> 6U;
  substream.bsid = frame[5] >> 3U;
  substream.acmod = (frame[4] >> 1U) & 7U;
  substream.lfeon = (frame[4] & 1U) != 0;
  substream.num_dependent_substreams = first.dependents;
  codec::Eac3Config config;
  const uint64_t bits_per_second = static_cast<uint64_t>(first.size) * 8U *
                                   static_cast<uint64_t>(first.sample_rate) /
                                   first.samples;
  config.data_rate = static_cast<uint16_t>(bits_per_second / 1000U);
  config.substreams.push_back(substream);
  ALUMBRA_ASSIGN_OR_RETURN(const std::vector<uint8_t> record,
                           codec::SerializeDec3(config));
  const codec::CodecDescription description = codec::ToCodec(config);
  penumbra::StreamInfo info;
  info.track_id = track_id;
  info.codec = codec::CodecIdOf(description);
  info.codec_string = codec::FormatCodecString(description);
  info.codec_config = penumbra::SharedBytes::Copy(record);
  info.time_base = kTimeBase;
  info.language = language;
  penumbra::AudioInfo audio = codec::ToAudioInfo(config);
  audio.sample_rate = first.sample_rate;
  info.format = audio;
  return info;
}

}

VideoScan ScanVideo(CaptionCodec codec, std::span<const uint8_t> payload) {
  VideoScan scan;
  const absl::StatusOr<std::vector<codec::Bytes>> units =
      codec::SplitAnnexB(payload);
  if (!units.ok()) {
    return scan;
  }
  for (const codec::Bytes unit : *units) {
    if (codec == CaptionCodec::kH264) {
      const uint8_t type = unit.front() & 0x1FU;
      scan.key_frame = scan.key_frame || type == 5;
      scan.has_sei = scan.has_sei || type == 6;
      scan.has_parameter_sets =
          scan.has_parameter_sets || type == 7 || type == 8;
    } else {
      const uint8_t type = (unit.front() >> 1U) & 0x3FU;
      scan.key_frame = scan.key_frame || (type >= 16 && type <= 21);
      scan.has_sei = scan.has_sei || type == 39 || type == 40;
      scan.has_parameter_sets =
          scan.has_parameter_sets || (type >= 32 && type <= 34);
    }
  }
  return scan;
}

absl::StatusOr<penumbra::StreamInfo> BuildVideoInfo(
    CaptionCodec codec, std::span<const uint8_t> payload, uint32_t track_id,
    const std::string& language) {
  penumbra::StreamInfo info;
  info.track_id = track_id;
  info.time_base = kTimeBase;
  info.language = language;
  if (codec == CaptionCodec::kH264) {
    ALUMBRA_ASSIGN_OR_RETURN(const codec::AvcConfig config,
                             codec::AvcConfigFromAnnexB(payload));
    ALUMBRA_ASSIGN_OR_RETURN(const codec::H264Sps sps,
                             codec::ParseH264Sps(config.sps.front()));
    ALUMBRA_ASSIGN_OR_RETURN(const std::vector<uint8_t> record,
                             codec::SerializeAvcC(config));
    const codec::CodecDescription description = codec::ToCodec(config);
    info.codec = codec::CodecIdOf(description);
    info.codec_string = codec::FormatCodecString(description);
    info.codec_config = penumbra::SharedBytes::Copy(record);
    info.nal_unit_framing = penumbra::NalUnitFraming::kAnnexB;
    info.nal_length_size = config.nal_length_size;
    info.format = codec::ToVideoInfo(sps);
    return info;
  }
  ALUMBRA_ASSIGN_OR_RETURN(const codec::HevcConfig config,
                           codec::HevcConfigFromAnnexB(payload));
  const codec::HevcNalArray* sps_array = nullptr;
  for (const codec::HevcNalArray& array : config.arrays) {
    if (array.nal_unit_type == codec::kHevcSpsNalType && !array.units.empty()) {
      sps_array = &array;
      break;
    }
  }
  if (sps_array == nullptr) {
    return codec::Corrupt("no HEVC SPS available");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const codec::HevcSps sps,
                           codec::ParseHevcSps(sps_array->units.front()));
  ALUMBRA_ASSIGN_OR_RETURN(const std::vector<uint8_t> record,
                           codec::SerializeHvcC(config));
  const codec::CodecDescription description = codec::ToCodec(config);
  info.codec = codec::CodecIdOf(description);
  info.codec_string = codec::FormatCodecString(description);
  info.codec_config = penumbra::SharedBytes::Copy(record);
  info.nal_unit_framing = penumbra::NalUnitFraming::kAnnexB;
  info.nal_length_size = config.nal_length_size;
  info.format = codec::ToVideoInfo(sps);
  return info;
}

AudioSplit SplitAudio(EsKind kind, std::span<const uint8_t> payload) {
  switch (kind) {
    case EsKind::kAdts:
      return SplitWith(payload, ParseAdts);
    case EsKind::kMpegAudio:
      return SplitWith(payload, ParseMpegAudioFrame);
    case EsKind::kAc3:
      return SplitWith(payload, ParseAc3);
    case EsKind::kEac3:
      return MergeDependents(SplitWith(payload, ParseEac3));
    default:
      return AudioSplit{};
  }
}

absl::StatusOr<penumbra::StreamInfo> BuildAudioInfo(
    EsKind kind, std::span<const uint8_t> frame, uint32_t track_id,
    const std::string& language) {
  switch (kind) {
    case EsKind::kAdts:
      return BuildAdtsInfo(frame, track_id, language);
    case EsKind::kMpegAudio:
      return BuildMpegAudioInfo(frame, track_id, language);
    case EsKind::kAc3:
      return BuildAc3Info(frame, track_id, language);
    case EsKind::kEac3:
      return BuildEac3Info(frame, track_id, language);
    default:
      return codec::Unsupported("stream kind has no audio configuration");
  }
}

}
