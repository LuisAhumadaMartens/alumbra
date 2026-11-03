#include "umbra/demux/packed_audio_demuxer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/aac.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/reader_util.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/ts_framing.h"

namespace umbra::demux {
namespace {

constexpr std::array<uint32_t, 13> kAdtsRates = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000,
    22050, 16000, 12000, 11025, 8000,  7350};
constexpr std::array<uint16_t, 16> kMpeg1Layer3Rates = {
    0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
constexpr std::array<uint16_t, 16> kMpeg2Layer3Rates = {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
constexpr size_t kMaxId3Size = static_cast<size_t>(16) * 1024 * 1024;

absl::Status Corrupt(const char* message) { return codec::Corrupt(message); }

uint32_t ReadSynchsafe(const std::vector<uint8_t>& bytes, size_t offset) {
  return (static_cast<uint32_t>(bytes[offset] & 0x7fU) << 21) |
         (static_cast<uint32_t>(bytes[offset + 1] & 0x7fU) << 14) |
         (static_cast<uint32_t>(bytes[offset + 2] & 0x7fU) << 7) |
         static_cast<uint32_t>(bytes[offset + 3] & 0x7fU);
}

size_t Id3FrameSize(std::span<const uint8_t> size_bytes, uint8_t version) {
  if (version == 4) {
    return (static_cast<size_t>(size_bytes[0] & 0x7fU) << 21) |
           (static_cast<size_t>(size_bytes[1] & 0x7fU) << 14) |
           (static_cast<size_t>(size_bytes[2] & 0x7fU) << 7) |
           (size_bytes[3] & 0x7fU);
  }
  return (static_cast<size_t>(size_bytes[0]) << 24) |
         (static_cast<size_t>(size_bytes[1]) << 16) |
         (static_cast<size_t>(size_bytes[2]) << 8) | size_bytes[3];
}

std::optional<uint64_t> ParsePrivateTimestamp(std::span<const uint8_t> frame) {
  constexpr std::string_view kTimestampOwner =
      "com.apple.streaming.transportStreamTimestamp";
  const std::string_view frame_text(reinterpret_cast<const char*>(frame.data()),
                                    frame.size());
  const size_t owner_end = frame_text.find('\0');
  if (owner_end == std::string_view::npos ||
      frame_text.substr(0, owner_end) != kTimestampOwner ||
      owner_end + 9 > frame.size()) {
    return std::nullopt;
  }
  uint64_t value = 0;
  for (size_t index = owner_end + 1; index < owner_end + 9; ++index) {
    value = (value << 8) | frame[index];
  }
  return value & ((uint64_t{1} << 33) - 1);
}

std::optional<uint64_t> FindPackedTimestamp(std::span<const uint8_t> tag) {
  if (tag.size() < 10 || tag[0] != 'I' || tag[1] != 'D' || tag[2] != '3') {
    return std::nullopt;
  }
  const uint8_t version = tag[3];
  size_t cursor = 10;
  const size_t body_end = tag.size() - ((tag[5] & 0x10U) != 0 ? 10 : 0);
  const std::string_view tag_text(reinterpret_cast<const char*>(tag.data()),
                                  tag.size());
  while (cursor + 10 <= body_end) {
    if (tag_text.substr(cursor, 4) == std::string_view("\0\0\0\0", 4)) {
      break;
    }
    const size_t frame_size = Id3FrameSize(tag.subspan(cursor + 4, 4), version);
    const bool private_frame = tag_text.substr(cursor, 4) == "PRIV";
    cursor += 10;
    if (frame_size > body_end - cursor) {
      break;
    }
    if (private_frame) {
      const std::optional<uint64_t> timestamp =
          ParsePrivateTimestamp(tag.subspan(cursor, frame_size));
      if (timestamp.has_value()) {
        return timestamp;
      }
    }
    cursor += frame_size;
  }
  return std::nullopt;
}

penumbra::Timestamp MakeTimestamp(int64_t value, penumbra::Rational base) {
  return penumbra::Timestamp{.value = value, .time_base = base};
}

class PackedAudioSeekMap final : public SeekMap {
 public:
  PackedAudioSeekMap(std::vector<SeekPoint> points,
                     penumbra::TimeDelta duration, bool seekable)
      : points_(std::move(points)), duration_(duration), seekable_(seekable) {}

  [[nodiscard]] bool IsSeekable() const override { return seekable_; }
  [[nodiscard]] penumbra::TimeDelta Duration() const override {
    return duration_;
  }
  [[nodiscard]] std::optional<SeekPoint> SeekPointFor(
      penumbra::MediaTime target) const override {
    if (points_.empty() || !target.is_finite()) {
      return std::nullopt;
    }
    std::optional<SeekPoint> result;
    for (const SeekPoint& point : points_) {
      if (point.time > target) {
        break;
      }
      result = point;
    }
    return result.has_value() ? result
                              : std::optional<SeekPoint>(points_.front());
  }

 private:
  std::vector<SeekPoint> points_;
  penumbra::TimeDelta duration_;
  bool seekable_ = false;
};

}

PackedAudioDemuxer::PackedAudioDemuxer(DemuxerOutput& output,
                                       PackedAudioKind kind)
    : output_(output), kind_(kind) {}

void PackedAudioDemuxer::SetSourceInfo(std::optional<uint64_t> size,
                                       bool seekable) {
  source_size_ = size;
  seekable_ = seekable;
}

uint64_t PackedAudioDemuxer::NextOffset() const { return queue_.EndOffset(); }

absl::Status PackedAudioDemuxer::Append(uint64_t offset,
                                        penumbra::SharedBytes bytes) {
  if (!failure_.ok()) {
    return failure_;
  }
  if (offset != queue_.EndOffset()) {
    return absl::FailedPreconditionError("packed audio append offset mismatch");
  }
  queue_.Append(std::move(bytes));
  return absl::OkStatus();
}

void PackedAudioDemuxer::EndOfInput() { ended_ = true; }

absl::StatusOr<PackedAudioDemuxer::FrameHeader> PackedAudioDemuxer::ParseHeader(
    const std::vector<uint8_t>& bytes) const {
  FrameHeader header;
  if (kind_ == PackedAudioKind::kAdts) {
    if (bytes.size() < 7) {
      return absl::OutOfRangeError("incomplete ADTS header");
    }
    if (bytes[0] != 0xffU || (bytes[1] & 0xf6U) != 0xf0U) {
      return absl::InvalidArgumentError("invalid ADTS sync");
    }
    const uint8_t profile = (bytes[2] >> 6) & 0x03U;
    const uint8_t frequency = (bytes[2] >> 2) & 0x0fU;
    const auto channel = static_cast<uint8_t>(((bytes[2] & 1U) << 2) |
                                              ((bytes[3] >> 6) & 0x03U));
    const size_t frame_size = (static_cast<size_t>(bytes[3] & 0x03U) << 11) |
                              (static_cast<size_t>(bytes[4]) << 3) |
                              (bytes[5] >> 5);
    const uint8_t blocks = bytes[6] & 0x03U;
    if (frequency >= kAdtsRates.size() || channel == 0 || frame_size < 7 ||
        frame_size > 8191) {
      return absl::InvalidArgumentError("invalid ADTS frame fields");
    }
    header.size = frame_size;
    header.sample_rate = kAdtsRates.at(frequency);
    header.samples = 1024U * (static_cast<uint32_t>(blocks) + 1U);
    header.channels = channel;
    header.object_type = static_cast<uint8_t>(profile + 1U);
    header.frequency_index = frequency;
    header.channel_configuration = channel;
    return header;
  }
  if (kind_ == PackedAudioKind::kAc3 || kind_ == PackedAudioKind::kEac3) {
    return ParseDolbyHeader(bytes);
  }
  if (bytes.size() < 4) {
    return absl::OutOfRangeError("incomplete MPEG audio header");
  }
  const uint32_t word = (static_cast<uint32_t>(bytes[0]) << 24) |
                        (static_cast<uint32_t>(bytes[1]) << 16) |
                        (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
  if ((word & 0xffe00000U) != 0xffe00000U) {
    return absl::InvalidArgumentError("invalid MPEG audio sync");
  }
  const auto version = static_cast<uint8_t>((word >> 19) & 0x03U);
  const auto layer = static_cast<uint8_t>((word >> 17) & 0x03U);
  const auto bitrate_index = static_cast<uint8_t>((word >> 12) & 0x0fU);
  const auto rate_index = static_cast<uint8_t>((word >> 10) & 0x03U);
  if (version == 1 || layer != 1 || bitrate_index == 0 || bitrate_index == 15 ||
      rate_index == 3) {
    return absl::InvalidArgumentError("unsupported MPEG audio frame header");
  }
  constexpr std::array<uint32_t, 3> kBaseRates = {44100, 48000, 32000};
  uint32_t rate = kBaseRates.at(rate_index);
  if (version == 2) {
    rate /= 2;
  } else if (version == 0) {
    rate /= 4;
  }
  const bool mpeg1 = version == 3;
  const uint32_t bitrate =
      (mpeg1 ? kMpeg1Layer3Rates : kMpeg2Layer3Rates).at(bitrate_index) * 1000U;
  const uint32_t samples = mpeg1 ? 1152U : 576U;
  const uint32_t padding = (word >> 9) & 1U;
  const auto frame_size = static_cast<size_t>(
      ((mpeg1 ? 144ULL : 72ULL) * bitrate / rate) + padding);
  if (frame_size < 24 || frame_size > 4096) {
    return absl::InvalidArgumentError("invalid MPEG audio frame size");
  }
  header.size = frame_size;
  header.sample_rate = rate;
  header.samples = samples;
  header.channels = ((word >> 6) & 0x03U) == 3 ? 1U : 2U;
  return header;
}

absl::StatusOr<PackedAudioDemuxer::FrameHeader>
PackedAudioDemuxer::ParseDolbyHeader(const std::vector<uint8_t>& bytes) const {
  if (bytes.size() < 8) {
    return absl::OutOfRangeError("incomplete Dolby audio header");
  }
  const ts::EsKind frame_kind =
      kind_ == PackedAudioKind::kAc3 ? ts::EsKind::kAc3 : ts::EsKind::kEac3;
  const std::optional<ts::AudioFrame> parsed =
      ts::ParseAudioFrame(frame_kind, bytes);
  if (!parsed.has_value()) {
    return absl::InvalidArgumentError("invalid Dolby audio frame header");
  }
  FrameHeader header;
  header.size = parsed->size;
  header.sample_rate = static_cast<uint32_t>(parsed->sample_rate);
  header.samples = parsed->samples;
  header.channels = 2;
  return header;
}

absl::StatusOr<std::optional<penumbra::StreamInfo>>
PackedAudioDemuxer::ParseFrameInfo(const penumbra::SharedBytes& frame,
                                   FrameHeader& header) const {
  if (kind_ != PackedAudioKind::kAc3 && kind_ != PackedAudioKind::kEac3) {
    return std::optional<penumbra::StreamInfo>();
  }
  const ts::EsKind frame_kind =
      kind_ == PackedAudioKind::kAc3 ? ts::EsKind::kAc3 : ts::EsKind::kEac3;
  absl::StatusOr<penumbra::StreamInfo> info =
      ts::BuildAudioInfo(frame_kind, frame.span(), 1, "");
  if (!info.ok()) {
    return info.status();
  }
  header.channels = static_cast<uint32_t>(
      std::get<penumbra::AudioInfo>(info->format).channel_count);
  return std::optional<penumbra::StreamInfo>(std::move(*info));
}

absl::Status PackedAudioDemuxer::ApplyInitialTimestamp(
    const FrameHeader& header) {
  if (frames_ != 0 || !timestamp_90khz_.has_value()) {
    return absl::OkStatus();
  }
  const auto media_base = penumbra::Rational::Create(1, header.sample_rate);
  const auto clock_base = penumbra::Rational::Create(1, 90000);
  if (!media_base.has_value() || !clock_base.has_value()) {
    return Corrupt("packed audio timestamp time base is invalid");
  }
  const auto sample_time =
      penumbra::Rescale(*timestamp_90khz_, *clock_base, *media_base,
                        penumbra::Rounding::kNearest);
  if (!sample_time.has_value() || *sample_time < 0) {
    return Corrupt("packed audio timestamp is out of range");
  }
  total_samples_ = static_cast<uint64_t>(*sample_time);
  return absl::OkStatus();
}

absl::Status PackedAudioDemuxer::Announce(
    const FrameHeader& header, const penumbra::StreamInfo* parsed_info) {
  if (initialized_) {
    return absl::OkStatus();
  }
  sample_rate_ = header.sample_rate;
  channels_ = header.channels;
  samples_per_frame_ = header.samples;
  if (parsed_info != nullptr) {
    penumbra::StreamInfo stream = *parsed_info;
    stream.track_id = 1;
    const auto stream_time_base = penumbra::Rational::Create(1, sample_rate_);
    if (!stream_time_base.has_value()) {
      return Corrupt("packed audio sample rate is invalid");
    }
    stream.time_base = *stream_time_base;
    output_.OnStreams({std::move(stream)});
    initialized_ = true;
    UpdateSeekMap();
    return absl::OkStatus();
  }
  penumbra::StreamInfo stream;
  stream.track_id = 1;
  switch (kind_) {
    case PackedAudioKind::kAdts:
      stream.codec = penumbra::CodecId::kAac;
      break;
    case PackedAudioKind::kMp3:
      stream.codec = penumbra::CodecId::kMp3;
      break;
    case PackedAudioKind::kAc3:
      stream.codec = penumbra::CodecId::kAc3;
      break;
    case PackedAudioKind::kEac3:
      stream.codec = penumbra::CodecId::kEac3;
      break;
  }
  const auto stream_time_base = penumbra::Rational::Create(1, sample_rate_);
  if (!stream_time_base.has_value()) {
    return Corrupt("packed audio sample rate is invalid");
  }
  stream.time_base = *stream_time_base;
  stream.format =
      penumbra::AudioInfo{.sample_rate = static_cast<int32_t>(sample_rate_),
                          .channel_count = static_cast<int32_t>(channels_),
                          .channel_mask = channels_ == 1 ? 4U : 3U,
                          .sample_format = penumbra::SampleFormat::kF32Planar};
  if (kind_ == PackedAudioKind::kAdts) {
    const auto first = static_cast<uint8_t>((header.object_type << 3) |
                                            (header.frequency_index >> 1));
    const auto second = static_cast<uint8_t>(
        (header.frequency_index << 7) | (header.channel_configuration << 3));
    const std::array<uint8_t, 2> asc = {first, second};
    stream.codec_config = penumbra::SharedBytes::Copy(asc);
    const auto parsed = codec::ParseAudioSpecificConfig(asc);
    if (parsed.ok()) {
      stream.codec_string = codec::FormatCodecString(codec::ToCodec(*parsed));
      stream.format = codec::ToAudioInfo(*parsed);
    }
  } else if (kind_ == PackedAudioKind::kMp3) {
    stream.codec_string = codec::FormatCodecString(codec::Mp4aCodec{
        .object_type_indication = 0x40, .audio_object_type = 34});
  }
  output_.OnStreams({stream});
  initialized_ = true;
  UpdateSeekMap();
  return absl::OkStatus();
}

absl::Status PackedAudioDemuxer::ProcessId3(size_t tag_size) {
  const auto bytes = queue_.Read(tag_size);
  if (!bytes.has_value()) {
    return Corrupt("truncated ID3 tag");
  }
  const std::optional<uint64_t> raw_timestamp =
      FindPackedTimestamp(bytes->span());
  if (raw_timestamp.has_value()) {
    constexpr int64_t kTimestampWrap = int64_t{1} << 33;
    auto unwrapped = static_cast<int64_t>(*raw_timestamp);
    if (timestamp_reference_90khz_.has_value()) {
      const int64_t shifted =
          *timestamp_reference_90khz_ - unwrapped + (kTimestampWrap / 2);
      int64_t cycles = shifted / kTimestampWrap;
      if (shifted < 0 && shifted % kTimestampWrap != 0) {
        --cycles;
      }
      unwrapped += cycles * kTimestampWrap;
    }
    timestamp_reference_90khz_ = unwrapped;
    timestamp_90khz_ = unwrapped;
  }
  if (!metadata_announced_) {
    MetadataStreamInfo stream;
    stream.stream_id = static_cast<uint32_t>(metadata_stream_id_);
    stream.kind = MetadataKind::kId3;
    stream.time_base = penumbra::kMicrosecondTimeBase;
    if (sample_rate_ > 0) {
      const auto time_base = penumbra::Rational::Create(1, sample_rate_);
      if (!time_base.has_value()) {
        return Corrupt("ID3 timestamp time base is invalid");
      }
      stream.time_base = *time_base;
    }
    output_.OnMetadataStream(stream);
    metadata_announced_ = true;
  }
  penumbra::Rational base = penumbra::kMicrosecondTimeBase;
  if (sample_rate_ > 0) {
    const auto time_base = penumbra::Rational::Create(1, sample_rate_);
    if (!time_base.has_value()) {
      return Corrupt("ID3 timestamp time base is invalid");
    }
    base = *time_base;
  }
  output_.OnTimedMetadata(TimedMetadataItem{
      .stream_id = static_cast<uint32_t>(metadata_stream_id_),
      .time = MakeTimestamp(static_cast<int64_t>(total_samples_), base),
      .duration = MakeTimestamp(penumbra::Timestamp::kUnknown, base),
      .payload = *bytes,
      .event_id = 0});
  return absl::OkStatus();
}

bool PackedAudioDemuxer::ParseXing(const std::vector<uint8_t>& frame,
                                   const FrameHeader& header) {
  if (kind_ != PackedAudioKind::kMp3 || xing_seen_ || frame.size() < 40) {
    return false;
  }
  const uint32_t word = (static_cast<uint32_t>(frame[0]) << 24) |
                        (static_cast<uint32_t>(frame[1]) << 16) |
                        (static_cast<uint32_t>(frame[2]) << 8) | frame[3];
  const auto version = static_cast<uint8_t>((word >> 19) & 3U);
  const auto mode = static_cast<uint8_t>((word >> 6) & 3U);
  size_t side_info = mode == 3 ? 9U : 17U;
  if (version == 3) {
    side_info = mode == 3 ? 17U : 32U;
  }
  const size_t xing_offset = 4U + side_info;
  if (xing_offset + 8U > frame.size()) {
    return false;
  }
  const std::string_view text(reinterpret_cast<const char*>(frame.data()),
                              frame.size());
  const std::string_view marker = text.substr(xing_offset, 4);
  if (marker != "Xing" && marker != "Info") {
    if (frame.size() >= 54 && text.substr(36, 4) == "VBRI") {
      const uint32_t count = (static_cast<uint32_t>(frame[50]) << 24) |
                             (static_cast<uint32_t>(frame[51]) << 16) |
                             (static_cast<uint32_t>(frame[52]) << 8) |
                             frame[53];
      if (count > 0) {
        duration_samples_ = static_cast<uint64_t>(count) * header.samples;
        xing_seen_ = true;
        return true;
      }
    }
    return false;
  }
  const uint32_t flags = (static_cast<uint32_t>(frame[xing_offset + 4]) << 24) |
                         (static_cast<uint32_t>(frame[xing_offset + 5]) << 16) |
                         (static_cast<uint32_t>(frame[xing_offset + 6]) << 8) |
                         frame[xing_offset + 7];
  size_t cursor = xing_offset + 8;
  if ((flags & 1U) != 0 && cursor + 4 <= frame.size()) {
    const uint32_t count = (static_cast<uint32_t>(frame[cursor]) << 24) |
                           (static_cast<uint32_t>(frame[cursor + 1]) << 16) |
                           (static_cast<uint32_t>(frame[cursor + 2]) << 8) |
                           frame[cursor + 3];
    duration_samples_ = static_cast<uint64_t>(count) * header.samples;
    cursor += 4;
  }
  if ((flags & 2U) != 0) {
    cursor += 4;
  }
  if ((flags & 4U) != 0) {
    cursor += 100;
  }
  if ((flags & 8U) != 0) {
    cursor += 4;
  }
  const std::string_view encoder =
      cursor + 4 <= frame.size() ? text.substr(cursor, 4) : std::string_view();
  if (cursor + 24 <= frame.size() &&
      (encoder == "LAME" || encoder == "Lavc" || encoder == "Lavf")) {
    encoder_delay_ = (static_cast<uint32_t>(frame[cursor + 21]) << 4) |
                     (frame[cursor + 22] >> 4);
    encoder_padding_ =
        (static_cast<uint32_t>(frame[cursor + 22] & 0x0fU) << 8) |
        frame[cursor + 23];
  }
  xing_seen_ = true;
  return true;
}

void PackedAudioDemuxer::UpdateSeekMap() {
  if (!initialized_ || sample_rate_ == 0) {
    return;
  }
  uint64_t count = duration_samples_ > 0 ? duration_samples_ : total_samples_;
  if (count > encoder_delay_ + encoder_padding_) {
    count -= encoder_delay_ + encoder_padding_;
  }
  const auto base = penumbra::Rational::Create(1, sample_rate_);
  if (!base.has_value()) {
    return;
  }
  const auto micros = penumbra::Rescale(static_cast<int64_t>(count), *base,
                                        penumbra::kMicrosecondTimeBase,
                                        penumbra::Rounding::kNearest);
  const penumbra::TimeDelta duration =
      micros.has_value() ? penumbra::TimeDelta::FromMicroseconds(*micros)
                         : penumbra::TimeDelta::Unknown();
  output_.OnSeekMap(
      std::make_shared<PackedAudioSeekMap>(points_, duration, seekable_));
}

absl::StatusOr<std::optional<StepResult>> PackedAudioDemuxer::StepId3v2(
    const std::vector<uint8_t>& prefix) {
  if (prefix.size() < 3 || prefix[0] != 'I' || prefix[1] != 'D' ||
      prefix[2] != '3') {
    return std::optional<StepResult>();
  }
  if (prefix.size() < 10) {
    if (ended_) {
      return Corrupt("truncated ID3 header");
    }
    return std::optional<StepResult>(
        StepResult::NeedMoreData(queue_.EndOffset(), 10 - prefix.size()));
  }
  if ((prefix[6] & 0x80U) != 0 || (prefix[7] & 0x80U) != 0 ||
      (prefix[8] & 0x80U) != 0 || (prefix[9] & 0x80U) != 0) {
    return Corrupt("invalid ID3 synchsafe size");
  }
  if (prefix[3] < 2 || prefix[3] > 4 || prefix[4] == 0xffU) {
    return Corrupt("unsupported ID3 version");
  }
  const uint32_t body_size = ReadSynchsafe(prefix, 6);
  const size_t total =
      10U + body_size + (((prefix[5] & 0x10U) != 0) ? 10U : 0U);
  if (total > kMaxId3Size) {
    return Corrupt("ID3 tag exceeds the size limit");
  }
  if (queue_.size() < total) {
    if (ended_) {
      return Corrupt("truncated ID3 tag");
    }
    return std::optional<StepResult>(
        StepResult::NeedMoreData(queue_.EndOffset(), total - queue_.size()));
  }
  absl::Status status = ProcessId3(total);
  if (!status.ok()) {
    return status;
  }
  return std::optional<StepResult>(StepResult::Yield());
}

absl::StatusOr<std::optional<StepResult>> PackedAudioDemuxer::StepId3v1(
    const std::vector<uint8_t>& prefix) {
  if (prefix.size() < 3 || prefix[0] != 'T' || prefix[1] != 'A' ||
      prefix[2] != 'G') {
    return std::optional<StepResult>();
  }
  const bool final_at_known_size = source_size_.has_value() &&
                                   *source_size_ >= 128 &&
                                   queue_.StartOffset() == *source_size_ - 128;
  const bool possible_unknown_size_trailer = !source_size_.has_value();
  if (!final_at_known_size && !possible_unknown_size_trailer) {
    return std::optional<StepResult>();
  }
  if (queue_.size() < 128) {
    if (ended_) {
      return Corrupt("truncated ID3v1 tag");
    }
    return std::optional<StepResult>(
        StepResult::NeedMoreData(queue_.EndOffset(), 128 - queue_.size()));
  }
  if (possible_unknown_size_trailer && !ended_) {
    if (queue_.size() == 128) {
      return std::optional<StepResult>(
          StepResult::NeedMoreData(queue_.EndOffset(), 1));
    }
    return std::optional<StepResult>();
  }
  absl::Status status = ProcessId3(128);
  if (!status.ok()) {
    return status;
  }
  return std::optional<StepResult>(StepResult::Yield());
}

absl::StatusOr<StepResult> PackedAudioDemuxer::Step() {
  if (!failure_.ok()) {
    return failure_;
  }
  if (queue_.size() < 3) {
    if (!ended_) {
      return StepResult::NeedMoreData(queue_.EndOffset(), 3 - queue_.size());
    }
    if (queue_.size() == 0 && initialized_) {
      UpdateSeekMap();
      return StepResult::EndOfStream();
    }
    return Corrupt(queue_.size() == 0 ? "packed audio contains no frames"
                                      : "truncated packed audio data");
  }
  const std::vector<uint8_t> prefix = queue_.Peek(10);
  const auto id3v1 = StepId3v1(prefix);
  if (!id3v1.ok()) {
    failure_ = id3v1.status();
    return failure_;
  }
  if (id3v1->has_value()) {
    return **id3v1;
  }
  const auto id3v2 = StepId3v2(prefix);
  if (!id3v2.ok()) {
    failure_ = id3v2.status();
    return failure_;
  }
  if (id3v2->has_value()) {
    return **id3v2;
  }
  return StepAudioFrame();
}

absl::StatusOr<StepResult> PackedAudioDemuxer::StepAudioFrame() {
  const std::vector<uint8_t> header_bytes =
      queue_.Peek(kind_ == PackedAudioKind::kAdts ? 7 : 8);
  const auto header = ParseHeader(header_bytes);
  if (!header.ok()) {
    if (header.status().code() == absl::StatusCode::kOutOfRange && !ended_) {
      return StepResult::NeedMoreData(queue_.EndOffset(), 1);
    }
    if (header.status().code() == absl::StatusCode::kInvalidArgument) {
      queue_.Discard(1);
      return StepResult::Yield();
    }
    failure_ = Corrupt("truncated packed audio frame header");
    return failure_;
  }
  FrameHeader frame_header = *header;
  if (queue_.size() < header->size) {
    if (ended_) {
      failure_ = Corrupt("truncated packed audio frame");
      return failure_;
    }
    return StepResult::NeedMoreData(queue_.EndOffset(),
                                    header->size - queue_.size());
  }
  const uint64_t frame_offset = queue_.StartOffset();
  const auto frame = queue_.Read(header->size);
  if (!frame.has_value()) {
    failure_ = Corrupt("packed audio frame is unavailable");
    return failure_;
  }
  const absl::StatusOr<std::optional<penumbra::StreamInfo>> parsed_info =
      ParseFrameInfo(*frame, frame_header);
  if (!parsed_info.ok()) {
    failure_ = parsed_info.status();
    return failure_;
  }
  if (initialized_ && (frame_header.sample_rate != sample_rate_ ||
                       frame_header.channels != channels_)) {
    failure_ = Corrupt("packed audio format changes between frames");
    return failure_;
  }
  const absl::Status timestamp_status = ApplyInitialTimestamp(frame_header);
  if (!timestamp_status.ok()) {
    failure_ = timestamp_status;
    return failure_;
  }
  const absl::Status announced = Announce(
      frame_header, parsed_info->has_value() ? &**parsed_info : nullptr);
  if (!announced.ok()) {
    failure_ = announced;
    return failure_;
  }
  if (kind_ == PackedAudioKind::kMp3 && frames_ == 0) {
    const bool parsed_index = ParseXing(
        std::vector<uint8_t>(frame->span().begin(), frame->span().end()),
        frame_header);
    if (parsed_index) {
      UpdateSeekMap();
      return StepResult::Yield();
    }
  }
  const absl::Status emitted = EmitFrame(frame_header, frame_offset, *frame);
  if (!emitted.ok()) {
    failure_ = emitted;
    return failure_;
  }
  return StepResult::Yield();
}

absl::Status PackedAudioDemuxer::EmitFrame(const FrameHeader& header,
                                           uint64_t frame_offset,
                                           penumbra::SharedBytes frame) {
  const auto base = penumbra::Rational::Create(1, sample_rate_);
  if (!base.has_value()) {
    return Corrupt("packed audio sample rate is invalid");
  }
  const auto start = static_cast<int64_t>(total_samples_);
  const auto duration = static_cast<int64_t>(header.samples);
  penumbra::EncodedSample sample;
  sample.track_id = 1;
  sample.data = std::move(frame);
  sample.pts = MakeTimestamp(start, *base);
  sample.dts = sample.pts;
  sample.duration = MakeTimestamp(duration, *base);
  sample.key_frame = true;
  sample.discontinuity = std::exchange(discontinuity_pending_, false);
  if (kind_ == PackedAudioKind::kMp3 && frames_ == 0 && encoder_delay_ > 0) {
    sample.trim_start = MakeTimestamp(encoder_delay_, *base);
  }
  if (kind_ == PackedAudioKind::kMp3 && encoder_padding_ > 0 &&
      duration_samples_ > 0 &&
      total_samples_ + header.samples >= duration_samples_) {
    sample.trim_end = MakeTimestamp(encoder_padding_, *base);
  }
  const auto micros =
      penumbra::Rescale(start, *base, penumbra::kMicrosecondTimeBase,
                        penumbra::Rounding::kNearest);
  if (!micros.has_value()) {
    return Corrupt("packed audio timestamp is out of range");
  }
  output_.OnSample(std::move(sample));
  points_.push_back(
      SeekPoint{.time = penumbra::MediaTime::FromMicroseconds(*micros),
                .byte_offset = frame_offset});
  total_samples_ += header.samples;
  ++frames_;
  UpdateSeekMap();
  return absl::OkStatus();
}

absl::StatusOr<uint64_t> PackedAudioDemuxer::Seek(const SeekPoint& point) {
  if (!seekable_) {
    return absl::FailedPreconditionError("packed audio source is not seekable");
  }
  const auto found = std::ranges::find(points_, point);
  if (found == points_.end()) {
    return absl::InvalidArgumentError("seek point is not in the seek map");
  }
  const auto base = penumbra::Rational::Create(1, sample_rate_);
  if (!base.has_value()) {
    return Corrupt("packed audio sample rate is invalid");
  }
  const auto sample = penumbra::Rescale(point.time.InMicroseconds(),
                                        penumbra::kMicrosecondTimeBase, *base,
                                        penumbra::Rounding::kDown);
  if (!sample.has_value()) {
    return absl::OutOfRangeError("packed audio seek time is out of range");
  }
  queue_.Reset(point.byte_offset);
  ended_ = false;
  failure_ = absl::OkStatus();
  total_samples_ = static_cast<uint64_t>(std::max<int64_t>(0, *sample));
  frames_ =
      static_cast<uint64_t>(std::ranges::distance(points_.begin(), found));
  discontinuity_pending_ = true;
  return point.byte_offset;
}

void PackedAudioDemuxer::Reset(uint64_t offset) {
  queue_.Reset(offset);
  ended_ = false;
  failure_ = absl::OkStatus();
  initialized_ = false;
  discontinuity_pending_ = true;
  metadata_announced_ = false;
  xing_seen_ = false;
  frames_ = 0;
  total_samples_ = 0;
  duration_samples_ = 0;
  encoder_delay_ = 0;
  encoder_padding_ = 0;
  timestamp_90khz_.reset();
  points_.clear();
}

}
