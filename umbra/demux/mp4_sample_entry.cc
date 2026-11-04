#include "umbra/demux/mp4_sample_entry.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/status.h"
#include "umbra/codec/aac.h"
#include "umbra/codec/ac3.h"
#include "umbra/codec/av1.h"
#include "umbra/codec/avc.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/flac.h"
#include "umbra/codec/hevc.h"
#include "umbra/codec/opus.h"
#include "umbra/codec/reader_util.h"
#include "umbra/codec/video_geometry.h"
#include "umbra/codec/vp9.h"
#include "umbra/demux/caption_extractor.h"
#include "umbra/demux/mp4_box.h"
#include "umbra/demux/mp4_types.h"

namespace umbra::demux {
namespace {

constexpr size_t kSampleEntryDepth = 7;
constexpr uint32_t kObjectTypeMp3Layer3 = 0x6B;
constexpr uint32_t kObjectTypeMp3Layer2 = 0x69;
constexpr uint8_t kEsDescriptorTag = 0x03;
constexpr uint8_t kDecoderConfigTag = 0x04;
constexpr uint8_t kDecoderSpecificTag = 0x05;
constexpr uint8_t kStreamDependenceFlag = 0x80;
constexpr uint8_t kUrlFlag = 0x40;
constexpr uint8_t kOcrStreamFlag = 0x20;
constexpr size_t kQuickTimeSoundV1Extra = 16;
constexpr size_t kQuickTimeSoundV2Extra = 36;

constexpr uint32_t kAvc1 = FourCc('a', 'v', 'c', '1');
constexpr uint32_t kAvc3 = FourCc('a', 'v', 'c', '3');
constexpr uint32_t kHvc1 = FourCc('h', 'v', 'c', '1');
constexpr uint32_t kHev1 = FourCc('h', 'e', 'v', '1');
constexpr uint32_t kVp09 = FourCc('v', 'p', '0', '9');
constexpr uint32_t kAv01 = FourCc('a', 'v', '0', '1');
constexpr uint32_t kMp4a = FourCc('m', 'p', '4', 'a');
constexpr uint32_t kTx3g = FourCc('t', 'x', '3', 'g');
constexpr uint32_t kWvtt = FourCc('w', 'v', 't', 't');
constexpr uint32_t kStpp = FourCc('s', 't', 'p', 'p');
constexpr uint32_t kOpus = FourCc('O', 'p', 'u', 's');
constexpr uint32_t kAc3 = FourCc('a', 'c', '-', '3');
constexpr uint32_t kEc3 = FourCc('e', 'c', '-', '3');
constexpr uint32_t kFlac = FourCc('f', 'L', 'a', 'C');
constexpr uint32_t kAvcC = FourCc('a', 'v', 'c', 'C');
constexpr uint32_t kHvcC = FourCc('h', 'v', 'c', 'C');
constexpr uint32_t kVpcC = FourCc('v', 'p', 'c', 'C');
constexpr uint32_t kAv1C = FourCc('a', 'v', '1', 'C');
constexpr uint32_t kEsds = FourCc('e', 's', 'd', 's');
constexpr uint32_t kDOps = FourCc('d', 'O', 'p', 's');
constexpr uint32_t kDac3 = FourCc('d', 'a', 'c', '3');
constexpr uint32_t kDec3 = FourCc('d', 'e', 'c', '3');
constexpr uint32_t kDfLa = FourCc('d', 'f', 'L', 'a');
constexpr uint32_t kPasp = FourCc('p', 'a', 's', 'p');
constexpr uint32_t kColr = FourCc('c', 'o', 'l', 'r');
constexpr uint32_t kNclx = FourCc('n', 'c', 'l', 'x');
constexpr uint32_t kMdcv = FourCc('m', 'd', 'c', 'v');
constexpr uint32_t kClli = FourCc('c', 'l', 'l', 'i');
constexpr uint32_t kDvcC = FourCc('d', 'v', 'c', 'C');
constexpr uint32_t kDvvC = FourCc('d', 'v', 'v', 'C');

std::string FourCcString(uint32_t fourcc) {
  std::string text(4, ' ');
  for (size_t index = 0; index < 4; ++index) {
    text[index] = static_cast<char>((fourcc >> (24 - 8 * index)) & 0xFF);
  }
  return text;
}

struct VisualEntry {
  int32_t width = 0;
  int32_t height = 0;
  std::vector<ChildBox> children;
};

struct AudioEntry {
  int32_t channels = 0;
  int32_t sample_rate = 0;
  std::vector<ChildBox> children;
};

const ChildBox* FindChild(const std::vector<ChildBox>& children,
                          uint32_t type) {
  for (const ChildBox& child : children) {
    if (child.type == type) {
      return &child;
    }
  }
  return nullptr;
}

absl::StatusOr<VisualEntry> ReadVisualEntry(std::span<const uint8_t> payload) {
  codec::RecordReader reader(payload, "visual sample entry");
  ALUMBRA_RETURN_IF_ERROR(reader.Skip(24));
  VisualEntry entry;
  ALUMBRA_ASSIGN_OR_RETURN(const uint16_t width, reader.U16());
  ALUMBRA_ASSIGN_OR_RETURN(const uint16_t height, reader.U16());
  ALUMBRA_RETURN_IF_ERROR(reader.Skip(50));
  entry.width = width;
  entry.height = height;
  ALUMBRA_ASSIGN_OR_RETURN(const std::span<const uint8_t> rest,
                           reader.Take(reader.remaining()));
  ALUMBRA_ASSIGN_OR_RETURN(entry.children,
                           SplitChildren(rest, kSampleEntryDepth));
  return entry;
}

absl::Status SkipSoundExtras(codec::RecordReader& reader, uint16_t version) {
  if (version == 1) {
    return reader.Skip(kQuickTimeSoundV1Extra);
  }
  if (version == 2) {
    return reader.Skip(kQuickTimeSoundV2Extra);
  }
  return absl::OkStatus();
}

absl::StatusOr<AudioEntry> ReadAudioEntry(std::span<const uint8_t> payload) {
  codec::RecordReader reader(payload, "audio sample entry");
  ALUMBRA_RETURN_IF_ERROR(reader.Skip(8));
  ALUMBRA_ASSIGN_OR_RETURN(const uint16_t version, reader.U16());
  ALUMBRA_RETURN_IF_ERROR(reader.Skip(6));
  AudioEntry entry;
  ALUMBRA_ASSIGN_OR_RETURN(const uint16_t channels, reader.U16());
  ALUMBRA_RETURN_IF_ERROR(reader.Skip(6));
  ALUMBRA_ASSIGN_OR_RETURN(const uint32_t rate, reader.U32());
  entry.channels = channels;
  entry.sample_rate = static_cast<int32_t>(rate >> 16);
  ALUMBRA_RETURN_IF_ERROR(SkipSoundExtras(reader, version));
  ALUMBRA_ASSIGN_OR_RETURN(const std::span<const uint8_t> rest,
                           reader.Take(reader.remaining()));
  ALUMBRA_ASSIGN_OR_RETURN(entry.children,
                           SplitChildren(rest, kSampleEntryDepth));
  return entry;
}

penumbra::VideoInfo FallbackVideoInfo(const VisualEntry& entry) {
  return codec::MakeVideoInfo(entry.width, entry.height, entry.width,
                              entry.height, penumbra::Rational::Of(1, 1),
                              penumbra::ColorSpace{});
}

void ApplyColr(const VisualEntry& entry, penumbra::VideoInfo& info) {
  const ChildBox* colr = FindChild(entry.children, kColr);
  if (colr == nullptr) {
    return;
  }
  codec::RecordReader reader(colr->payload, "colr");
  const absl::StatusOr<uint32_t> type = reader.U32();
  if (!type.ok() || *type != kNclx) {
    return;
  }
  const absl::StatusOr<uint16_t> primaries = reader.U16();
  const absl::StatusOr<uint16_t> transfer = reader.U16();
  const absl::StatusOr<uint16_t> matrix = reader.U16();
  const absl::StatusOr<uint8_t> range = reader.U8();
  if (!primaries.ok() || !transfer.ok() || !matrix.ok() || !range.ok() ||
      *primaries > 255 || *transfer > 255 || *matrix > 255) {
    return;
  }
  if (info.color.primaries == penumbra::ColorSpace::kUnspecified) {
    info.color.primaries = static_cast<uint8_t>(*primaries);
  }
  if (info.color.transfer == penumbra::ColorSpace::kUnspecified) {
    info.color.transfer = static_cast<uint8_t>(*transfer);
  }
  if (info.color.matrix == penumbra::ColorSpace::kUnspecified) {
    info.color.matrix = static_cast<uint8_t>(*matrix);
  }
  if (!info.color.range_specified) {
    info.color.full_range = (*range & 0x80) != 0;
    info.color.range_specified = true;
  }
}

void ApplyHdrStaticMetadata(const VisualEntry& entry,
                            penumbra::VideoInfo& info) {
  if (const ChildBox* box = FindChild(entry.children, kMdcv);
      box != nullptr && box->payload.size() == 24) {
    codec::RecordReader reader(box->payload, "mdcv");
    penumbra::MasteringDisplayMetadata metadata;
    bool valid = true;
    for (std::size_t index = 0; index < metadata.display_primaries_x.size();
         ++index) {
      const absl::StatusOr<uint16_t> x = reader.U16();
      const absl::StatusOr<uint16_t> y = reader.U16();
      if (!x.ok() || !y.ok()) {
        valid = false;
        break;
      }
      metadata.display_primaries_x.at(index) = *x;
      metadata.display_primaries_y.at(index) = *y;
    }
    const absl::StatusOr<uint16_t> white_x = reader.U16();
    const absl::StatusOr<uint16_t> white_y = reader.U16();
    const absl::StatusOr<uint32_t> max_luminance = reader.U32();
    const absl::StatusOr<uint32_t> min_luminance = reader.U32();
    if (valid && white_x.ok() && white_y.ok() && max_luminance.ok() &&
        min_luminance.ok()) {
      metadata.white_point_x = *white_x;
      metadata.white_point_y = *white_y;
      metadata.max_luminance = *max_luminance;
      metadata.min_luminance = *min_luminance;
      info.hdr_static_metadata.mastering_display = metadata;
    }
  }
  if (const ChildBox* box = FindChild(entry.children, kClli);
      box != nullptr && box->payload.size() == 4) {
    codec::RecordReader reader(box->payload, "clli");
    const absl::StatusOr<uint16_t> max_cll = reader.U16();
    const absl::StatusOr<uint16_t> max_fall = reader.U16();
    if (max_cll.ok() && max_fall.ok()) {
      info.hdr_static_metadata.max_content_light_level = *max_cll;
      info.hdr_static_metadata.max_frame_average_light_level = *max_fall;
    }
  }
}

void ApplyDolbyVisionConfig(const VisualEntry& entry,
                            penumbra::VideoInfo& info) {
  const ChildBox* box = FindChild(entry.children, kDvcC);
  if (box == nullptr) {
    box = FindChild(entry.children, kDvvC);
  }
  if (box == nullptr || box->payload.size() < 5) {
    return;
  }
  const uint8_t profile_level = box->payload[2];
  const uint8_t compatibility = box->payload[3];
  info.dolby_vision = penumbra::DolbyVisionConfig{
      .profile = static_cast<uint8_t>(profile_level >> 1),
      .level = static_cast<uint8_t>(((profile_level & 1U) << 5) |
                                    (compatibility >> 3)),
      .base_layer_compatibility_id =
          static_cast<uint8_t>(box->payload[4] >> 4)};
}

void ApplyPasp(const VisualEntry& entry, penumbra::VideoInfo& info) {
  const ChildBox* pasp = FindChild(entry.children, kPasp);
  if (pasp == nullptr ||
      info.sample_aspect_ratio != penumbra::Rational::Of(1, 1)) {
    return;
  }
  codec::RecordReader reader(pasp->payload, "pasp");
  const absl::StatusOr<uint32_t> horizontal = reader.U32();
  const absl::StatusOr<uint32_t> vertical = reader.U32();
  if (!horizontal.ok() || !vertical.ok() || *horizontal == 0 ||
      *vertical == 0) {
    return;
  }
  const std::optional<penumbra::Rational> ratio =
      penumbra::Rational::Create(*horizontal, *vertical);
  if (!ratio.has_value()) {
    return;
  }
  info.sample_aspect_ratio = *ratio;
  info.display_width = codec::ScaleByAspectRatio(info.display_width, *ratio);
}

void FinishVideoInfo(const VisualEntry& entry, penumbra::VideoInfo& info) {
  ApplyPasp(entry, info);
  ApplyColr(entry, info);
  ApplyHdrStaticMetadata(entry, info);
  ApplyDolbyVisionConfig(entry, info);
}

SampleEntryInfo MakeEntry(
    const SampleEntryContext& context,
    const codec::CodecDescription& description, std::span<const uint8_t> config,
    std::variant<penumbra::VideoInfo, penumbra::AudioInfo> format,
    uint8_t nal_length_size = 0) {
  penumbra::StreamInfo stream;
  stream.track_id = context.track_id;
  stream.codec = codec::CodecIdOf(description);
  stream.codec_string = codec::FormatCodecString(description);
  stream.codec_config = penumbra::SharedBytes::Copy(config);
  stream.nal_length_size = nal_length_size;
  if (stream.codec == penumbra::CodecId::kH264 ||
      stream.codec == penumbra::CodecId::kHevc) {
    stream.nal_unit_framing = penumbra::NalUnitFraming::kLengthPrefixed;
  }
  if (stream.codec == penumbra::CodecId::kAac) {
    stream.aac_framing = penumbra::AacFraming::kRaw;
  }
  stream.time_base = context.time_base;
  stream.language = context.language;
  if (std::holds_alternative<penumbra::VideoInfo>(format)) {
    stream.format = std::get<penumbra::VideoInfo>(format);
  } else {
    stream.format = std::get<penumbra::AudioInfo>(format);
  }
  SampleEntryInfo entry;
  entry.stream = std::move(stream);
  return entry;
}

absl::StatusOr<SampleEntryInfo> BuildAvc(uint32_t fourcc,
                                         const VisualEntry& visual,
                                         const SampleEntryContext& context) {
  const ChildBox* box = FindChild(visual.children, kAvcC);
  if (box == nullptr) {
    return codec::Corrupt("avc sample entry has no avcC box");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const codec::AvcConfig config,
                           codec::ParseAvcC(box->payload));
  penumbra::VideoInfo info = FallbackVideoInfo(visual);
  if (!config.sps.empty()) {
    const absl::StatusOr<codec::H264Sps> sps =
        codec::ParseH264Sps(config.sps.front());
    if (sps.ok()) {
      info = codec::ToVideoInfo(*sps);
    }
  }
  FinishVideoInfo(visual, info);
  codec::AvcCodec description = codec::ToCodec(config);
  description.sample_entry = FourCcString(fourcc);
  SampleEntryInfo entry = MakeEntry(context, description, box->payload, info,
                                    config.nal_length_size);
  entry.nal_length_size = config.nal_length_size;
  entry.caption_codec = CaptionCodec::kH264;
  return entry;
}

absl::StatusOr<SampleEntryInfo> BuildHevc(uint32_t fourcc,
                                          const VisualEntry& visual,
                                          const SampleEntryContext& context) {
  const ChildBox* box = FindChild(visual.children, kHvcC);
  if (box == nullptr) {
    return codec::Corrupt("hevc sample entry has no hvcC box");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const codec::HevcConfig config,
                           codec::ParseHvcC(box->payload));
  penumbra::VideoInfo info = FallbackVideoInfo(visual);
  for (const codec::HevcNalArray& array : config.arrays) {
    if (array.nal_unit_type != codec::kHevcSpsNalType || array.units.empty()) {
      continue;
    }
    const absl::StatusOr<codec::HevcSps> sps =
        codec::ParseHevcSps(array.units.front());
    if (sps.ok()) {
      info = codec::ToVideoInfo(*sps);
    }
    break;
  }
  FinishVideoInfo(visual, info);
  SampleEntryInfo entry =
      MakeEntry(context, codec::ToCodec(config, FourCcString(fourcc)),
                box->payload, info, config.nal_length_size);
  entry.nal_length_size = config.nal_length_size;
  entry.caption_codec = CaptionCodec::kHevc;
  return entry;
}

absl::StatusOr<SampleEntryInfo> BuildVp9(const VisualEntry& visual,
                                         const SampleEntryContext& context) {
  const ChildBox* box = FindChild(visual.children, kVpcC);
  if (box == nullptr) {
    return codec::Corrupt("vp09 sample entry has no vpcC box");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const codec::Vp9Config config,
                           codec::ParseVpcC(box->payload));
  penumbra::VideoInfo info = FallbackVideoInfo(visual);
  info.color = codec::ToColorSpace(config);
  FinishVideoInfo(visual, info);
  return MakeEntry(context, codec::ToCodec(config), box->payload, info);
}

absl::StatusOr<SampleEntryInfo> BuildAv1(const VisualEntry& visual,
                                         const SampleEntryContext& context) {
  const ChildBox* box = FindChild(visual.children, kAv1C);
  if (box == nullptr) {
    return codec::Corrupt("av01 sample entry has no av1C box");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const codec::Av1Config config,
                           codec::ParseAv1C(box->payload));
  penumbra::VideoInfo info = FallbackVideoInfo(visual);
  if (config.sequence_header.has_value()) {
    const penumbra::VideoInfo from_header =
        codec::ToVideoInfo(*config.sequence_header);
    info.color = from_header.color;
  }
  FinishVideoInfo(visual, info);
  return MakeEntry(context, codec::ToCodec(config), box->payload, info);
}

penumbra::AudioInfo EntryAudioInfo(const AudioEntry& entry) {
  penumbra::AudioInfo info;
  info.sample_rate = entry.sample_rate;
  info.channel_count = entry.channels;
  return info;
}

absl::StatusOr<SampleEntryInfo> BuildMp4a(const AudioEntry& audio,
                                          const SampleEntryContext& context) {
  const ChildBox* box = FindChild(audio.children, kEsds);
  if (box == nullptr) {
    return codec::Corrupt("mp4a sample entry has no esds box");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const EsdsInfo esds, ParseEsds(box->payload));
  if (esds.object_type_indication == kObjectTypeMp3Layer3 ||
      esds.object_type_indication == kObjectTypeMp3Layer2) {
    codec::Mp4aCodec description;
    description.object_type_indication = esds.object_type_indication;
    return MakeEntry(context, description, {}, EntryAudioInfo(audio));
  }
  ALUMBRA_ASSIGN_OR_RETURN(
      const codec::AacConfig config,
      codec::ParseAudioSpecificConfig(esds.decoder_specific_info));
  codec::Mp4aCodec description = codec::ToCodec(config);
  description.object_type_indication = esds.object_type_indication;
  return MakeEntry(context, description, esds.decoder_specific_info,
                   codec::ToAudioInfo(config));
}

absl::StatusOr<SampleEntryInfo> BuildOpus(const AudioEntry& audio,
                                          const SampleEntryContext& context) {
  const ChildBox* box = FindChild(audio.children, kDOps);
  if (box == nullptr) {
    return codec::Corrupt("Opus sample entry has no dOps box");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const codec::OpusConfig config,
                           codec::ParseDOps(box->payload));
  return MakeEntry(context, codec::ToCodec(config), box->payload,
                   codec::ToAudioInfo(config));
}

absl::StatusOr<SampleEntryInfo> BuildAc3(const AudioEntry& audio,
                                         const SampleEntryContext& context) {
  const ChildBox* box = FindChild(audio.children, kDac3);
  if (box == nullptr) {
    return codec::Corrupt("ac-3 sample entry has no dac3 box");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const codec::Ac3Config config,
                           codec::ParseDac3(box->payload));
  penumbra::AudioInfo info = codec::ToAudioInfo(config);
  if (info.sample_rate == 0) {
    info.sample_rate = audio.sample_rate;
  }
  return MakeEntry(context, codec::ToCodec(config), box->payload, info);
}

absl::StatusOr<SampleEntryInfo> BuildEac3(const AudioEntry& audio,
                                          const SampleEntryContext& context) {
  const ChildBox* box = FindChild(audio.children, kDec3);
  if (box == nullptr) {
    return codec::Corrupt("ec-3 sample entry has no dec3 box");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const codec::Eac3Config config,
                           codec::ParseDec3(box->payload));
  penumbra::AudioInfo info = codec::ToAudioInfo(config);
  if (info.sample_rate == 0) {
    info.sample_rate = audio.sample_rate;
  }
  return MakeEntry(context, codec::ToCodec(config), box->payload, info);
}

absl::StatusOr<SampleEntryInfo> BuildFlac(const AudioEntry& audio,
                                          const SampleEntryContext& context) {
  const ChildBox* box = FindChild(audio.children, kDfLa);
  if (box == nullptr) {
    return codec::Corrupt("fLaC sample entry has no dfLa box");
  }
  ALUMBRA_ASSIGN_OR_RETURN(const codec::FlacStreamInfo info,
                           codec::ParseDfLa(box->payload));
  return MakeEntry(context, codec::ToCodec(info), box->payload,
                   codec::ToAudioInfo(info));
}

absl::StatusOr<SampleEntryInfo> ParseVisual(uint32_t fourcc,
                                            std::span<const uint8_t> payload,
                                            const SampleEntryContext& context) {
  ALUMBRA_ASSIGN_OR_RETURN(const VisualEntry visual, ReadVisualEntry(payload));
  if (fourcc == kAvc1 || fourcc == kAvc3) {
    return BuildAvc(fourcc, visual, context);
  }
  if (fourcc == kHvc1 || fourcc == kHev1) {
    return BuildHevc(fourcc, visual, context);
  }
  if (fourcc == kVp09) {
    return BuildVp9(visual, context);
  }
  return BuildAv1(visual, context);
}

absl::StatusOr<SampleEntryInfo> ParseAudio(uint32_t fourcc,
                                           std::span<const uint8_t> payload,
                                           const SampleEntryContext& context) {
  ALUMBRA_ASSIGN_OR_RETURN(const AudioEntry audio, ReadAudioEntry(payload));
  if (fourcc == kMp4a) {
    return BuildMp4a(audio, context);
  }
  if (fourcc == kOpus) {
    return BuildOpus(audio, context);
  }
  if (fourcc == kAc3) {
    return BuildAc3(audio, context);
  }
  if (fourcc == kEc3) {
    return BuildEac3(audio, context);
  }
  return BuildFlac(audio, context);
}

bool IsVisualFourCc(uint32_t fourcc) {
  return fourcc == kAvc1 || fourcc == kAvc3 || fourcc == kHvc1 ||
         fourcc == kHev1 || fourcc == kVp09 || fourcc == kAv01;
}

bool IsAudioFourCc(uint32_t fourcc) {
  return fourcc == kMp4a || fourcc == kOpus || fourcc == kAc3 ||
         fourcc == kEc3 || fourcc == kFlac;
}

absl::StatusOr<uint8_t> ReadDescriptorTag(codec::RecordReader& reader,
                                          size_t& size) {
  ALUMBRA_ASSIGN_OR_RETURN(const uint8_t tag, reader.U8());
  size = 0;
  for (int index = 0; index < 4; ++index) {
    ALUMBRA_ASSIGN_OR_RETURN(const uint8_t byte, reader.U8());
    size = (size << 7) | (byte & 0x7F);
    if ((byte & 0x80) == 0) {
      return tag;
    }
  }
  return codec::Corrupt("descriptor length is too long");
}

absl::Status SkipEsDescriptorFields(codec::RecordReader& reader) {
  ALUMBRA_RETURN_IF_ERROR(reader.Skip(2));
  ALUMBRA_ASSIGN_OR_RETURN(const uint8_t flags, reader.U8());
  if ((flags & kStreamDependenceFlag) != 0) {
    ALUMBRA_RETURN_IF_ERROR(reader.Skip(2));
  }
  if ((flags & kUrlFlag) != 0) {
    ALUMBRA_ASSIGN_OR_RETURN(const uint8_t length, reader.U8());
    ALUMBRA_RETURN_IF_ERROR(reader.Skip(length));
  }
  if ((flags & kOcrStreamFlag) != 0) {
    ALUMBRA_RETURN_IF_ERROR(reader.Skip(2));
  }
  return absl::OkStatus();
}

}

absl::StatusOr<EsdsInfo> ParseEsds(std::span<const uint8_t> payload) {
  codec::RecordReader reader(payload, "esds");
  ALUMBRA_RETURN_IF_ERROR(reader.Skip(4));
  size_t size = 0;
  ALUMBRA_ASSIGN_OR_RETURN(const uint8_t es_tag,
                           ReadDescriptorTag(reader, size));
  if (es_tag != kEsDescriptorTag) {
    return codec::Corrupt("esds does not start with an ES descriptor");
  }
  ALUMBRA_RETURN_IF_ERROR(SkipEsDescriptorFields(reader));
  ALUMBRA_ASSIGN_OR_RETURN(const uint8_t config_tag,
                           ReadDescriptorTag(reader, size));
  if (config_tag != kDecoderConfigTag) {
    return codec::Corrupt("esds has no decoder configuration descriptor");
  }
  EsdsInfo info;
  ALUMBRA_ASSIGN_OR_RETURN(info.object_type_indication, reader.U8());
  ALUMBRA_RETURN_IF_ERROR(reader.Skip(12));
  if (reader.remaining() == 0) {
    return info;
  }
  ALUMBRA_ASSIGN_OR_RETURN(const uint8_t specific_tag,
                           ReadDescriptorTag(reader, size));
  if (specific_tag != kDecoderSpecificTag) {
    return info;
  }
  ALUMBRA_ASSIGN_OR_RETURN(const std::span<const uint8_t> bytes,
                           reader.Take(size));
  info.decoder_specific_info.assign(bytes.begin(), bytes.end());
  return info;
}

absl::StatusOr<SampleEntryInfo> ParseSampleEntry(
    uint32_t fourcc, std::span<const uint8_t> payload,
    const SampleEntryContext& context) {
  if (fourcc == kTx3g || fourcc == kWvtt || fourcc == kStpp) {
    penumbra::StreamInfo stream;
    stream.track_id = context.track_id;
    stream.codec_string = FourCcString(fourcc);
    stream.time_base = context.time_base;
    stream.language = context.language;
    stream.format = penumbra::TextInfo{};
    SampleEntryInfo entry;
    entry.stream = std::move(stream);
    return entry;
  }
  if (IsVisualFourCc(fourcc)) {
    return ParseVisual(fourcc, payload, context);
  }
  if (IsAudioFourCc(fourcc)) {
    return ParseAudio(fourcc, payload, context);
  }
  return SampleEntryInfo{};
}

}
