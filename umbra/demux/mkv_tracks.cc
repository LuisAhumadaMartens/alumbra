#include "umbra/demux/mkv_tracks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/status.h"
#include "umbra/codec/aac.h"
#include "umbra/codec/av1.h"
#include "umbra/codec/avc.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/flac.h"
#include "umbra/codec/hevc.h"
#include "umbra/codec/opus.h"
#include "umbra/codec/reader_util.h"
#include "umbra/codec/video_geometry.h"
#include "umbra/codec/vp9.h"
#include "umbra/demux/ebml.h"
#include "umbra/demux/mkv_elements.h"

namespace umbra::demux {
namespace {

constexpr uint64_t kMaxDimension = 65536;
constexpr uint8_t kObjectTypeMp3Layer3 = 0x6B;
constexpr uint8_t kObjectTypeMp3Layer2 = 0x69;
constexpr size_t kFlacMagicBytes = 4;
constexpr size_t kFlacBlockHeaderBytes = 4;
constexpr int64_t kNanosecondsPerSecond = 1000000000;
constexpr int64_t kOpusMaxPacketTicks = 5760;

struct Vp9Level {
  uint8_t level = 0;
  uint64_t max_luma_picture_size = 0;
};

constexpr std::array<Vp9Level, 9> kVp9Levels = {{
    {.level = 10, .max_luma_picture_size = 36864},
    {.level = 11, .max_luma_picture_size = 73728},
    {.level = 20, .max_luma_picture_size = 122880},
    {.level = 21, .max_luma_picture_size = 245760},
    {.level = 30, .max_luma_picture_size = 552960},
    {.level = 31, .max_luma_picture_size = 983040},
    {.level = 40, .max_luma_picture_size = 2228224},
    {.level = 50, .max_luma_picture_size = 8912896},
    {.level = 60, .max_luma_picture_size = 35651584},
}};

constexpr uint8_t kVp9HighestLevel = 62;

struct Built {
  codec::CodecDescription description;
  std::vector<uint8_t> config;
  std::variant<penumbra::VideoInfo, penumbra::AudioInfo> format;
  penumbra::NalUnitFraming nal_unit_framing =
      penumbra::NalUnitFraming::kUnspecified;
  uint8_t nal_length_size = 0;
  penumbra::AacFraming aac_framing = penumbra::AacFraming::kUnspecified;
  bool opus = false;
};

penumbra::Rational NanosecondBase() {
  return penumbra::Rational::Of(1, kNanosecondsPerSecond);
}

penumbra::ColorSpace ColorOf(const std::optional<MkvColour>& colour) {
  penumbra::ColorSpace color;
  if (!colour.has_value()) {
    return color;
  }
  const auto narrow = [](const std::optional<uint64_t>& value,
                         uint8_t fallback) {
    if (value.has_value() && *value <= std::numeric_limits<uint8_t>::max()) {
      return static_cast<uint8_t>(*value);
    }
    return fallback;
  };
  color.primaries = narrow(colour->primaries, color.primaries);
  color.transfer = narrow(colour->transfer, color.transfer);
  color.matrix = narrow(colour->matrix, color.matrix);
  color.full_range = colour->range.has_value() && *colour->range == 2;
  color.range_specified = colour->range.has_value();
  return color;
}

penumbra::Rational AspectRatioOf(const MkvVideo& video) {
  const penumbra::Rational square = penumbra::Rational::Of(1, 1);
  if (video.display_unit != 0 || !video.display_width.has_value() ||
      !video.display_height.has_value() || *video.display_width == 0 ||
      *video.display_height == 0 || *video.display_width > kMaxDimension ||
      *video.display_height > kMaxDimension) {
    return square;
  }
  const std::optional<penumbra::Rational> ratio = penumbra::Rational::Create(
      static_cast<int64_t>(*video.display_width * video.pixel_height),
      static_cast<int64_t>(*video.display_height * video.pixel_width));
  return ratio.value_or(square);
}

absl::StatusOr<penumbra::VideoInfo> FallbackVideoInfo(const MkvTrack& track) {
  const MkvVideo& video = track.video;
  if (video.pixel_width == 0 || video.pixel_height == 0 ||
      video.pixel_width > kMaxDimension || video.pixel_height > kMaxDimension) {
    return codec::Corrupt("video track has invalid dimensions");
  }
  const auto width = static_cast<int32_t>(video.pixel_width);
  const auto height = static_cast<int32_t>(video.pixel_height);
  penumbra::VideoInfo info =
      codec::MakeVideoInfo(width, height, width, height, AspectRatioOf(video),
                           ColorOf(video.colour));
  if (track.default_duration_ns.has_value() &&
      *track.default_duration_ns != 0 &&
      *track.default_duration_ns <=
          static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    info.frame_rate = penumbra::Rational::Create(
                          kNanosecondsPerSecond,
                          static_cast<int64_t>(*track.default_duration_ns))
                          .value_or(penumbra::Rational());
  }
  return info;
}

void ApplyContainerVideo(const MkvTrack& track, penumbra::VideoInfo& info) {
  const penumbra::Rational square = penumbra::Rational::Of(1, 1);
  const penumbra::Rational ratio = AspectRatioOf(track.video);
  if (info.sample_aspect_ratio == square && ratio != square) {
    info.sample_aspect_ratio = ratio;
    info.display_width = codec::ScaleByAspectRatio(info.display_width, ratio);
  }
  if (info.color == penumbra::ColorSpace{}) {
    info.color = ColorOf(track.video.colour);
  }
}

absl::StatusOr<std::array<std::span<const uint8_t>, 3>> XiphHeaders(
    const std::vector<uint8_t>& data) {
  if (data.empty() || data[0] != 2) {
    return codec::Corrupt("Xiph codec private must contain three headers");
  }
  size_t position = 1;
  std::array<size_t, 2> sizes{};
  for (size_t& size : sizes) {
    size = 0;
    uint8_t byte = 255;
    while (byte == 255) {
      if (position >= data.size()) {
        return codec::Corrupt("Xiph codec private header sizes are truncated");
      }
      byte = data[position++];
      if (size > std::numeric_limits<size_t>::max() - byte) {
        return codec::Corrupt("Xiph codec private header size overflows");
      }
      size += byte;
    }
  }
  if (sizes[0] == 0 || sizes[1] == 0 || position >= data.size() ||
      sizes[0] > data.size() - position) {
    return codec::Corrupt("Xiph codec private headers are empty or truncated");
  }
  position += sizes[0];
  if (sizes[1] > data.size() - position ||
      data.size() - position - sizes[1] == 0) {
    return codec::Corrupt("Xiph codec private headers are truncated");
  }
  const size_t third_size = data.size() - position - sizes[1];
  return std::array<std::span<const uint8_t>, 3>{
      std::span<const uint8_t>(data).subspan(position - sizes[0], sizes[0]),
      std::span<const uint8_t>(data).subspan(position, sizes[1]),
      std::span<const uint8_t>(data).subspan(position + sizes[1], third_size)};
}

bool HasHeader(std::span<const uint8_t> header, uint8_t type,
               std::string_view signature) {
  if (header.size() < signature.size() + 1 || header[0] != type) {
    return false;
  }
  return std::equal(signature.begin(), signature.end(), header.begin() + 1,
                    [](char expected, uint8_t actual) {
                      return static_cast<uint8_t>(expected) == actual;
                    });
}

absl::StatusOr<Built> BuildAvc(const MkvTrack& track,
                               penumbra::VideoInfo info) {
  ALUMBRA_ASSIGN_OR_RETURN(const codec::AvcConfig config,
                           codec::ParseAvcC(track.codec_private));
  if (!config.sps.empty()) {
    const absl::StatusOr<codec::H264Sps> sps =
        codec::ParseH264Sps(config.sps.front());
    if (sps.ok()) {
      const penumbra::Rational rate = info.frame_rate;
      info = codec::ToVideoInfo(*sps);
      info.frame_rate = rate;
    }
  }
  ApplyContainerVideo(track, info);
  Built built;
  built.description = codec::ToCodec(config);
  built.config = track.codec_private;
  built.format = info;
  built.nal_unit_framing = penumbra::NalUnitFraming::kLengthPrefixed;
  built.nal_length_size = config.nal_length_size;
  return built;
}

absl::StatusOr<Built> BuildHevc(const MkvTrack& track,
                                penumbra::VideoInfo info) {
  ALUMBRA_ASSIGN_OR_RETURN(const codec::HevcConfig config,
                           codec::ParseHvcC(track.codec_private));
  for (const codec::HevcNalArray& array : config.arrays) {
    if (array.nal_unit_type != codec::kHevcSpsNalType || array.units.empty()) {
      continue;
    }
    const absl::StatusOr<codec::HevcSps> sps =
        codec::ParseHevcSps(array.units.front());
    if (sps.ok()) {
      const penumbra::Rational rate = info.frame_rate;
      info = codec::ToVideoInfo(*sps);
      info.frame_rate = rate;
    }
    break;
  }
  ApplyContainerVideo(track, info);
  Built built;
  built.description = codec::ToCodec(config, "hvc1");
  built.config = track.codec_private;
  built.format = info;
  built.nal_unit_framing = penumbra::NalUnitFraming::kLengthPrefixed;
  built.nal_length_size = config.nal_length_size;
  return built;
}

uint8_t Vp9LevelFor(const MkvVideo& video) {
  const uint64_t pixels = video.pixel_width * video.pixel_height;
  for (const Vp9Level& entry : kVp9Levels) {
    if (pixels <= entry.max_luma_picture_size) {
      return entry.level;
    }
  }
  return kVp9HighestLevel;
}

void ApplyVp9Private(const std::vector<uint8_t>& data, codec::Vp9Config& config,
                     bool& has_level) {
  codec::Vp9Config parsed = config;
  bool level_seen = false;
  size_t position = 0;
  while (position + 2 <= data.size()) {
    const uint8_t id = data[position];
    const uint8_t length = data[position + 1];
    position += 2;
    if (length != 1 || position + length > data.size()) {
      return;
    }
    const uint8_t value = data[position];
    position += length;
    if (id == 1) {
      parsed.profile = value;
    } else if (id == 2) {
      parsed.level = value;
      level_seen = true;
    } else if (id == 3) {
      parsed.bit_depth = value;
    } else if (id == 4) {
      parsed.chroma_subsampling = value;
    }
  }
  config = parsed;
  has_level = level_seen;
}

absl::StatusOr<Built> BuildVp9(const MkvTrack& track,
                               penumbra::VideoInfo info) {
  codec::Vp9Config config;
  bool has_level = false;
  ApplyVp9Private(track.codec_private, config, has_level);
  if (!has_level) {
    config.level = Vp9LevelFor(track.video);
  }
  if (track.video.colour.has_value()) {
    const penumbra::ColorSpace color = ColorOf(track.video.colour);
    config.colour_primaries = color.primaries;
    config.transfer_characteristics = color.transfer;
    config.matrix_coefficients = color.matrix;
    config.full_range = color.full_range;
    info.color = codec::ToColorSpace(config);
  }
  ALUMBRA_ASSIGN_OR_RETURN(std::vector<uint8_t> record,
                           codec::SerializeVpcC(config));
  Built built;
  built.description = codec::ToCodec(config);
  built.config = std::move(record);
  built.format = info;
  return built;
}

absl::StatusOr<Built> BuildAv1(const MkvTrack& track,
                               penumbra::VideoInfo info) {
  ALUMBRA_ASSIGN_OR_RETURN(const codec::Av1Config config,
                           codec::ParseAv1C(track.codec_private));
  if (config.sequence_header.has_value()) {
    info.color = codec::ToVideoInfo(*config.sequence_header).color;
  }
  ApplyContainerVideo(track, info);
  Built built;
  built.description = codec::ToCodec(config);
  built.config = track.codec_private;
  built.format = info;
  return built;
}

absl::StatusOr<Built> BuildVp8(const MkvTrack& track,
                               penumbra::VideoInfo info) {
  ApplyContainerVideo(track, info);
  Built built;
  built.description = codec::Vp8Codec{};
  built.format = info;
  return built;
}

absl::StatusOr<Built> BuildTheora(const MkvTrack& track,
                                  penumbra::VideoInfo info) {
  ALUMBRA_ASSIGN_OR_RETURN(const auto headers,
                           XiphHeaders(track.codec_private));
  if (!HasHeader(headers[0], 0x80, "theora") ||
      !HasHeader(headers[1], 0x81, "theora") ||
      !HasHeader(headers[2], 0x82, "theora")) {
    return codec::Corrupt("Theora codec private has invalid Xiph headers");
  }
  ApplyContainerVideo(track, info);
  Built built;
  built.description = codec::TheoraCodec{};
  built.config = track.codec_private;
  built.format = info;
  return built;
}

penumbra::AudioInfo ContainerAudioInfo(const MkvTrack& track) {
  penumbra::AudioInfo info;
  info.sample_rate =
      static_cast<int32_t>(std::lround(track.audio.sampling_frequency));
  info.channel_count = static_cast<int32_t>(track.audio.channels);
  return info;
}

absl::StatusOr<Built> BuildVorbis(const MkvTrack& track) {
  ALUMBRA_ASSIGN_OR_RETURN(const auto headers,
                           XiphHeaders(track.codec_private));
  if (!HasHeader(headers[0], 1, "vorbis") ||
      !HasHeader(headers[1], 3, "vorbis") ||
      !HasHeader(headers[2], 5, "vorbis")) {
    return codec::Corrupt("Vorbis codec private has invalid Xiph headers");
  }
  Built built;
  built.description = codec::VorbisCodec{};
  built.config = track.codec_private;
  built.format = ContainerAudioInfo(track);
  return built;
}

absl::StatusOr<Built> BuildOpus(const MkvTrack& track) {
  ALUMBRA_ASSIGN_OR_RETURN(const codec::OpusConfig config,
                           codec::ParseOpusHead(track.codec_private));
  Built built;
  built.description = codec::ToCodec(config);
  built.config = track.codec_private;
  built.format = codec::ToAudioInfo(config);
  built.opus = true;
  return built;
}

absl::StatusOr<Built> BuildAac(const MkvTrack& track) {
  ALUMBRA_ASSIGN_OR_RETURN(
      const codec::AacConfig config,
      codec::ParseAudioSpecificConfig(track.codec_private));
  Built built;
  built.description = codec::ToCodec(config);
  built.config = track.codec_private;
  built.format = codec::ToAudioInfo(config);
  built.aac_framing = penumbra::AacFraming::kRaw;
  return built;
}

absl::StatusOr<Built> BuildFlac(const MkvTrack& track) {
  const std::vector<uint8_t>& data = track.codec_private;
  constexpr size_t kPrefix = kFlacMagicBytes + kFlacBlockHeaderBytes;
  if (data.size() < kPrefix + codec::kFlacStreamInfoSize || data[0] != 'f' ||
      data[1] != 'L' || data[2] != 'a' || data[3] != 'C' ||
      (data[kFlacMagicBytes] & 0x7F) != 0) {
    return codec::Corrupt("FLAC codec private does not start with STREAMINFO");
  }
  ALUMBRA_ASSIGN_OR_RETURN(
      const codec::FlacStreamInfo info,
      codec::ParseFlacStreamInfo(
          std::span<const uint8_t>(data).subspan(kPrefix)));
  Built built;
  built.description = codec::ToCodec(info);
  built.config = data;
  built.format = codec::ToAudioInfo(info);
  return built;
}

absl::StatusOr<std::optional<Built>> BuildVideo(const MkvTrack& track) {
  const std::string& id = track.codec_id;
  const bool known = id == "V_MPEG4/ISO/AVC" || id == "V_MPEGH/ISO/HEVC" ||
                     id == "V_VP8" || id == "V_VP9" || id == "V_AV1" ||
                     id == "V_THEORA";
  if (!known) {
    return std::optional<Built>();
  }
  ALUMBRA_ASSIGN_OR_RETURN(const penumbra::VideoInfo info,
                           FallbackVideoInfo(track));
  absl::StatusOr<Built> built = codec::Unsupported("video codec");
  if (id == "V_MPEG4/ISO/AVC") {
    built = BuildAvc(track, info);
  } else if (id == "V_MPEGH/ISO/HEVC") {
    built = BuildHevc(track, info);
  } else if (id == "V_VP8") {
    built = BuildVp8(track, info);
  } else if (id == "V_VP9") {
    built = BuildVp9(track, info);
  } else if (id == "V_THEORA") {
    built = BuildTheora(track, info);
  } else {
    built = BuildAv1(track, info);
  }
  if (!built.ok()) {
    return built.status();
  }
  return std::optional<Built>(*std::move(built));
}

absl::StatusOr<std::optional<Built>> BuildAudio(const MkvTrack& track) {
  const std::string& id = track.codec_id;
  absl::StatusOr<Built> built = codec::Unsupported("audio codec");
  if (id == "A_VORBIS") {
    built = BuildVorbis(track);
  } else if (id == "A_OPUS") {
    built = BuildOpus(track);
  } else if (id == "A_AAC") {
    built = BuildAac(track);
  } else if (id == "A_FLAC") {
    built = BuildFlac(track);
  } else if (id == "A_AC3" || id == "A_EAC3") {
    Built plain;
    if (id == "A_AC3") {
      plain.description = codec::Ac3Codec{};
    } else {
      plain.description = codec::Eac3Codec{};
    }
    plain.format = ContainerAudioInfo(track);
    built = std::move(plain);
  } else if (id == "A_MPEG/L3" || id == "A_MPEG/L2") {
    codec::Mp4aCodec description;
    description.object_type_indication =
        id == "A_MPEG/L3" ? kObjectTypeMp3Layer3 : kObjectTypeMp3Layer2;
    Built plain;
    plain.description = description;
    plain.format = ContainerAudioInfo(track);
    built = std::move(plain);
  } else {
    return std::optional<Built>();
  }
  if (!built.ok()) {
    return built.status();
  }
  return std::optional<Built>(*std::move(built));
}

absl::StatusOr<int64_t> ToSignedNanoseconds(uint64_t value) {
  if (value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return codec::Corrupt("duration is out of range");
  }
  return static_cast<int64_t>(value);
}

}

absl::StatusOr<std::optional<MkvTrackConfig>> BuildTrackConfig(
    const MkvTrack& track, penumbra::Rational scale_base) {
  if (track.encrypted || track.unsupported_encoding) {
    return std::optional<MkvTrackConfig>();
  }
  if (track.type == kTrackTypeSubtitle &&
      (track.codec_id == "S_TEXT/UTF8" || track.codec_id == "S_TEXT/ASS" ||
       track.codec_id == "S_TEXT/SSA" || track.codec_id == "S_TEXT/WEBVTT")) {
    MkvTrackConfig config;
    config.number = track.number;
    config.stream.track_id = static_cast<uint32_t>(track.number);
    if (track.codec_id == "S_TEXT/UTF8") {
      config.stream.codec_string = "srt";
    } else if (track.codec_id == "S_TEXT/ASS") {
      config.stream.codec_string = "ass";
    } else if (track.codec_id == "S_TEXT/SSA") {
      config.stream.codec_string = "ssa";
    } else {
      config.stream.codec_string = "webvtt";
    }
    config.stream.codec_config =
        penumbra::SharedBytes::Copy(track.codec_private);
    config.stream.time_base = scale_base;
    config.stream.language = track.language;
    config.stream.format = penumbra::TextInfo{};
    return std::optional<MkvTrackConfig>(std::move(config));
  }
  absl::StatusOr<std::optional<Built>> built = std::optional<Built>();
  if (track.type == kTrackTypeVideo) {
    built = BuildVideo(track);
  } else if (track.type == kTrackTypeAudio) {
    built = BuildAudio(track);
  }
  if (!built.ok()) {
    return built.status();
  }
  if (!built->has_value()) {
    return std::optional<MkvTrackConfig>();
  }
  Built& source = **built;
  MkvTrackConfig config;
  config.number = track.number;
  config.opus = source.opus;
  config.stream.track_id = static_cast<uint32_t>(track.number);
  config.stream.codec = codec::CodecIdOf(source.description);
  config.stream.codec_string = codec::FormatCodecString(source.description);
  config.stream.codec_config = penumbra::SharedBytes::Copy(source.config);
  config.stream.nal_unit_framing = source.nal_unit_framing;
  config.stream.nal_length_size = source.nal_length_size;
  config.stream.aac_framing = source.aac_framing;
  config.stream.time_base =
      source.opus ? penumbra::Rational::Of(1, codec::kOpusOutputSampleRate)
                  : scale_base;
  config.stream.language = track.language;
  if (std::holds_alternative<penumbra::VideoInfo>(source.format)) {
    config.stream.format = std::get<penumbra::VideoInfo>(source.format);
  } else {
    config.stream.format = std::get<penumbra::AudioInfo>(source.format);
  }
  const penumbra::Rational base = config.stream.time_base;
  if (track.default_duration_ns.has_value() &&
      *track.default_duration_ns != 0) {
    ALUMBRA_ASSIGN_OR_RETURN(const int64_t nanoseconds,
                             ToSignedNanoseconds(*track.default_duration_ns));
    config.default_duration_ns = nanoseconds;
  }
  ALUMBRA_ASSIGN_OR_RETURN(const int64_t delay,
                           ToSignedNanoseconds(track.codec_delay_ns));
  const std::optional<int64_t> delay_ticks = penumbra::Rescale(
      delay, NanosecondBase(), base, penumbra::Rounding::kNearest);
  if (!delay_ticks.has_value()) {
    return codec::Corrupt("codec delay is out of range");
  }
  config.codec_delay_ticks = *delay_ticks;
  ALUMBRA_ASSIGN_OR_RETURN(const int64_t pre_roll,
                           ToSignedNanoseconds(track.seek_pre_roll_ns));
  config.seek_pre_roll_micros =
      penumbra::Rescale(pre_roll, NanosecondBase(),
                        penumbra::kMicrosecondTimeBase,
                        penumbra::Rounding::kNearest)
          .value_or(0);
  config.header_strip = track.header_strip;
  return std::optional<MkvTrackConfig>(std::move(config));
}

std::optional<int64_t> OpusPacketTicks(EbmlBytes packet) {
  constexpr std::array<int64_t, 4> kSilk = {480, 960, 1920, 2880};
  constexpr std::array<int64_t, 4> kHybrid = {480, 960, 480, 960};
  constexpr std::array<int64_t, 4> kCelt = {120, 240, 480, 960};
  if (packet.empty()) {
    return std::nullopt;
  }
  const uint8_t config = packet[0] >> 3;
  int64_t frame_ticks = kCelt.at(config % 4);
  if (config < 12) {
    frame_ticks = kSilk.at(config % 4);
  } else if (config < 16) {
    frame_ticks = kHybrid.at(config % 4);
  }
  const uint8_t code = packet[0] & 0x03;
  int64_t frames = 1;
  if (code == 1 || code == 2) {
    frames = 2;
  } else if (code == 3) {
    if (packet.size() < 2) {
      return std::nullopt;
    }
    frames = packet[1] & 0x3F;
  }
  const int64_t total = frames * frame_ticks;
  if (frames == 0 || total > kOpusMaxPacketTicks) {
    return std::nullopt;
  }
  return total;
}

}
