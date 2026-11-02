#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fuzz_target.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/aac.h"
#include "umbra/codec/ac3.h"
#include "umbra/codec/av1.h"
#include "umbra/codec/avc.h"
#include "umbra/codec/codec_string.h"
#include "umbra/codec/flac.h"
#include "umbra/codec/hevc.h"
#include "umbra/codec/nal_unit.h"
#include "umbra/codec/opus.h"
#include "umbra/codec/reader_util.h"
#include "umbra/codec/vp9.h"

namespace {

using Bytes = umbra::codec::Bytes;

constexpr int kTargetCount = 19;

void Check(bool condition) {
  if (!condition) {
    std::abort();
  }
}

void CheckError(const absl::Status& status) {
  Check(status.code() == absl::StatusCode::kDataLoss ||
        status.code() == absl::StatusCode::kUnimplemented ||
        status.code() == absl::StatusCode::kInvalidArgument);
}

template <typename T>
bool Succeeded(const absl::StatusOr<T>& value) {
  if (!value.ok()) {
    CheckError(value.status());
  }
  return value.ok();
}

void CheckCodecStringRoundTrip(const umbra::codec::CodecDescription& codec) {
  const std::string text = umbra::codec::FormatCodecString(codec);
  const absl::StatusOr<umbra::codec::CodecDescription> again =
      umbra::codec::ParseCodecString(text);
  Check(again.ok());
  Check(*again == codec);
  Check(umbra::codec::FormatCodecString(*again) == text);
  (void)umbra::codec::CodecIdOf(codec);
}

void CheckGeometry(std::int32_t coded_width, std::int32_t coded_height,
                   std::int32_t visible_width, std::int32_t visible_height) {
  Check(visible_width > 0 && visible_height > 0);
  Check(visible_width <= coded_width && visible_height <= coded_height);
}

void FuzzAvcC(Bytes data) {
  const absl::StatusOr<umbra::codec::AvcConfig> config =
      umbra::codec::ParseAvcC(data);
  if (!Succeeded(config)) {
    return;
  }
  CheckCodecStringRoundTrip(umbra::codec::ToCodec(*config));
  const absl::StatusOr<std::vector<std::uint8_t>> first =
      umbra::codec::SerializeAvcC(*config);
  if (!first.ok()) {
    return;
  }
  const absl::StatusOr<umbra::codec::AvcConfig> reparsed =
      umbra::codec::ParseAvcC(*first);
  Check(reparsed.ok());
  const absl::StatusOr<std::vector<std::uint8_t>> second =
      umbra::codec::SerializeAvcC(*reparsed);
  Check(second.ok() && *second == *first);
}

void FuzzH264Sps(Bytes data) {
  const absl::StatusOr<umbra::codec::H264Sps> sps =
      umbra::codec::ParseH264Sps(data);
  if (!Succeeded(sps)) {
    return;
  }
  CheckGeometry(sps->coded_width, sps->coded_height, sps->visible_width,
                sps->visible_height);
  CheckCodecStringRoundTrip(umbra::codec::ToCodec(*sps));
  const penumbra::VideoInfo info = umbra::codec::ToVideoInfo(*sps);
  Check(info.display_width > 0 && info.display_height > 0);
}

void FuzzHvcC(Bytes data) {
  const absl::StatusOr<umbra::codec::HevcConfig> config =
      umbra::codec::ParseHvcC(data);
  if (!Succeeded(config)) {
    return;
  }
  CheckCodecStringRoundTrip(umbra::codec::ToCodec(*config));
  const absl::StatusOr<std::vector<std::uint8_t>> first =
      umbra::codec::SerializeHvcC(*config);
  if (!first.ok()) {
    return;
  }
  const absl::StatusOr<umbra::codec::HevcConfig> reparsed =
      umbra::codec::ParseHvcC(*first);
  Check(reparsed.ok());
  const absl::StatusOr<std::vector<std::uint8_t>> second =
      umbra::codec::SerializeHvcC(*reparsed);
  Check(second.ok() && *second == *first);
}

void FuzzHevcSps(Bytes data) {
  const absl::StatusOr<umbra::codec::HevcSps> sps =
      umbra::codec::ParseHevcSps(data);
  if (!Succeeded(sps)) {
    return;
  }
  CheckGeometry(sps->coded_width, sps->coded_height, sps->visible_width,
                sps->visible_height);
  CheckCodecStringRoundTrip(umbra::codec::ToCodec(*sps));
  const penumbra::VideoInfo info = umbra::codec::ToVideoInfo(*sps);
  Check(info.display_width > 0 && info.display_height > 0);
}

void FuzzHevcVps(Bytes data) {
  const absl::StatusOr<umbra::codec::HevcVps> vps =
      umbra::codec::ParseHevcVps(data);
  (void)Succeeded(vps);
}

void FuzzVpcC(Bytes data) {
  const absl::StatusOr<umbra::codec::Vp9Config> config =
      umbra::codec::ParseVpcC(data);
  if (!Succeeded(config)) {
    return;
  }
  CheckCodecStringRoundTrip(umbra::codec::ToCodec(*config));
  const absl::StatusOr<std::vector<std::uint8_t>> record =
      umbra::codec::SerializeVpcC(*config);
  Check(record.ok());
  const absl::StatusOr<umbra::codec::Vp9Config> again =
      umbra::codec::ParseVpcC(*record);
  Check(again.ok() && *again == *config);
}

void FuzzAv1C(Bytes data) {
  const absl::StatusOr<umbra::codec::Av1Config> config =
      umbra::codec::ParseAv1C(data);
  if (!Succeeded(config)) {
    return;
  }
  CheckCodecStringRoundTrip(umbra::codec::ToCodec(*config));
  const absl::StatusOr<std::vector<std::uint8_t>> record =
      umbra::codec::SerializeAv1C(*config);
  if (record.ok()) {
    Check(umbra::codec::ParseAv1C(*record).ok());
  }
}

void FuzzAv1SequenceHeader(Bytes data) {
  const absl::StatusOr<umbra::codec::Av1SequenceHeader> header =
      umbra::codec::ParseAv1SequenceHeaderObu(data);
  if (Succeeded(header)) {
    CheckCodecStringRoundTrip(umbra::codec::ToCodec(*header));
    (void)umbra::codec::ToVideoInfo(*header);
  }
  (void)Succeeded(umbra::codec::FindAv1SequenceHeader(data));
  (void)Succeeded(umbra::codec::Av1ConfigFromSequenceHeaderObu(data));
}

void FuzzAudioSpecificConfig(Bytes data) {
  const absl::StatusOr<umbra::codec::AacConfig> config =
      umbra::codec::ParseAudioSpecificConfig(data);
  if (!Succeeded(config)) {
    return;
  }
  Check(config->channel_count > 0);
  CheckCodecStringRoundTrip(umbra::codec::ToCodec(*config));
  const penumbra::AudioInfo info = umbra::codec::ToAudioInfo(*config);
  Check(info.sample_rate > 0 && info.channel_count > 0);
}

void CheckOpus(const absl::StatusOr<umbra::codec::OpusConfig>& config) {
  if (!Succeeded(config)) {
    return;
  }
  const absl::StatusOr<std::vector<std::uint8_t>> box =
      umbra::codec::SerializeDOps(*config);
  Check(box.ok());
  const absl::StatusOr<umbra::codec::OpusConfig> again =
      umbra::codec::ParseDOps(*box);
  Check(again.ok() && *again == *config);
  CheckCodecStringRoundTrip(umbra::codec::ToCodec(*config));
}

void FuzzOpusHead(Bytes data) { CheckOpus(umbra::codec::ParseOpusHead(data)); }

void FuzzDOps(Bytes data) { CheckOpus(umbra::codec::ParseDOps(data)); }

void FuzzDac3(Bytes data) {
  const absl::StatusOr<umbra::codec::Ac3Config> config =
      umbra::codec::ParseDac3(data);
  if (!Succeeded(config)) {
    return;
  }
  const absl::StatusOr<std::vector<std::uint8_t>> box =
      umbra::codec::SerializeDac3(*config);
  Check(box.ok());
  const absl::StatusOr<umbra::codec::Ac3Config> again =
      umbra::codec::ParseDac3(*box);
  Check(again.ok() && *again == *config);
  CheckCodecStringRoundTrip(umbra::codec::ToCodec(*config));
  Check(umbra::codec::ToAudioInfo(*config).channel_count > 0);
}

void FuzzDec3(Bytes data) {
  const absl::StatusOr<umbra::codec::Eac3Config> config =
      umbra::codec::ParseDec3(data);
  if (!Succeeded(config)) {
    return;
  }
  const absl::StatusOr<std::vector<std::uint8_t>> box =
      umbra::codec::SerializeDec3(*config);
  if (box.ok()) {
    const absl::StatusOr<umbra::codec::Eac3Config> again =
        umbra::codec::ParseDec3(*box);
    Check(again.ok());
  }
  CheckCodecStringRoundTrip(umbra::codec::ToCodec(*config));
  Check(umbra::codec::ToAudioInfo(*config).channel_count > 0);
}

void CheckFlac(const absl::StatusOr<umbra::codec::FlacStreamInfo>& info) {
  if (!Succeeded(info)) {
    return;
  }
  Check(info->sample_rate > 0 && info->channel_count >= 1 &&
        info->channel_count <= 8 && info->bits_per_sample >= 4);
  CheckCodecStringRoundTrip(umbra::codec::ToCodec(*info));
  (void)umbra::codec::ToAudioInfo(*info);
}

void FuzzFlacStreamInfo(Bytes data) {
  CheckFlac(umbra::codec::ParseFlacStreamInfo(data));
}

void FuzzDfLa(Bytes data) { CheckFlac(umbra::codec::ParseDfLa(data)); }

bool Contains(Bytes outer, Bytes inner) {
  const auto outer_begin = reinterpret_cast<std::uintptr_t>(outer.data());
  const auto inner_begin = reinterpret_cast<std::uintptr_t>(inner.data());
  return inner_begin >= outer_begin &&
         inner_begin - outer_begin + inner.size() <= outer.size();
}

void FuzzNalFraming(Bytes data) {
  const absl::StatusOr<std::vector<Bytes>> units =
      umbra::codec::SplitAnnexB(data);
  if (Succeeded(units)) {
    for (const Bytes unit : *units) {
      Check(!unit.empty() && Contains(data, unit));
    }
    const absl::StatusOr<std::vector<std::uint8_t>> prefixed =
        umbra::codec::AnnexBToLengthPrefixed(data, 4);
    if (Succeeded(prefixed)) {
      const absl::StatusOr<std::vector<Bytes>> back =
          umbra::codec::SplitLengthPrefixed(*prefixed, 4);
      Check(back.ok() && back->size() == units->size());
      for (std::size_t i = 0; i < units->size(); ++i) {
        Check(std::equal((*units)[i].begin(), (*units)[i].end(),
                         (*back)[i].begin(), (*back)[i].end()));
      }
    }
  }
  for (const std::size_t length_size :
       {std::size_t{1}, std::size_t{2}, std::size_t{4}}) {
    const absl::StatusOr<std::vector<std::uint8_t>> annex_b =
        umbra::codec::LengthPrefixedToAnnexB(data, length_size);
    if (Succeeded(annex_b) && !annex_b->empty()) {
      Check(umbra::codec::SplitAnnexB(*annex_b).ok());
    }
  }
  (void)Succeeded(umbra::codec::AvcConfigFromAnnexB(data));
  (void)Succeeded(umbra::codec::HevcConfigFromAnnexB(data));
}

void FuzzEmulationPrevention(Bytes data) {
  const std::vector<std::uint8_t> clean =
      umbra::codec::RemoveEmulationPrevention(data);
  Check(clean.size() <= data.size());
  const std::size_t removed = data.size() - clean.size();
  std::size_t zeros = 0;
  std::size_t threes = 0;
  for (const std::uint8_t byte : data) {
    if (zeros >= 2 && byte == 3) {
      ++threes;
      zeros = 0;
    } else {
      zeros = byte == 0 ? zeros + 1 : 0;
    }
  }
  Check(removed == threes);
}

void FuzzCodecString(Bytes data) {
  const std::string_view text(reinterpret_cast<const char*>(data.data()),
                              data.size());
  const absl::StatusOr<umbra::codec::CodecDescription> codec =
      umbra::codec::ParseCodecString(text);
  if (Succeeded(codec)) {
    CheckCodecStringRoundTrip(*codec);
  }
}

void FuzzCodecList(Bytes data) {
  const std::string_view text(reinterpret_cast<const char*>(data.data()),
                              data.size());
  const absl::StatusOr<std::vector<umbra::codec::CodecDescription>> list =
      umbra::codec::ParseCodecList(text);
  if (Succeeded(list)) {
    for (const umbra::codec::CodecDescription& codec : *list) {
      CheckCodecStringRoundTrip(codec);
    }
  }
}

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const Bytes input(data, size);
  if (input.empty()) {
    return 0;
  }
  const Bytes payload = input.subspan(1);
  switch (input.front() % kTargetCount) {
    case 0:
      FuzzAvcC(payload);
      break;
    case 1:
      FuzzH264Sps(payload);
      break;
    case 2:
      FuzzHvcC(payload);
      break;
    case 3:
      FuzzHevcSps(payload);
      break;
    case 4:
      FuzzHevcVps(payload);
      break;
    case 5:
      FuzzVpcC(payload);
      break;
    case 6:
      FuzzAv1C(payload);
      break;
    case 7:
      FuzzAv1SequenceHeader(payload);
      break;
    case 8:
      FuzzAudioSpecificConfig(payload);
      break;
    case 9:
      FuzzOpusHead(payload);
      break;
    case 10:
      FuzzDOps(payload);
      break;
    case 11:
      FuzzDac3(payload);
      break;
    case 12:
      FuzzDec3(payload);
      break;
    case 13:
      FuzzFlacStreamInfo(payload);
      break;
    case 14:
      FuzzDfLa(payload);
      break;
    case 15:
      FuzzNalFraming(payload);
      break;
    case 16:
      FuzzEmulationPrevention(payload);
      break;
    case 17:
      FuzzCodecString(payload);
      break;
    default:
      FuzzCodecList(payload);
      break;
  }
  return 0;
}
