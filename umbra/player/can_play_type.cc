#include "umbra/player/can_play_type.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_split.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/codec_string.h"
#include "umbra/player/media_state.h"

namespace umbra::player::internal {
namespace {

using penumbra::CodecId;

struct Container {
  std::string_view essence;
  std::vector<CodecId> codecs;
};

const std::array<Container, 8>& Containers() {
  static const std::array<Container, 8> kContainers{{
      {.essence = "video/mp4",
       .codecs = {CodecId::kH264, CodecId::kHevc, CodecId::kVp9, CodecId::kAv1,
                  CodecId::kAac, CodecId::kOpus, CodecId::kAc3, CodecId::kEac3,
                  CodecId::kFlac, CodecId::kMp3}},
      {.essence = "audio/mp4",
       .codecs = {CodecId::kAac, CodecId::kOpus, CodecId::kAc3, CodecId::kEac3,
                  CodecId::kFlac, CodecId::kMp3}},
      {.essence = "video/webm",
       .codecs = {CodecId::kVp8, CodecId::kVp9, CodecId::kAv1, CodecId::kVorbis,
                  CodecId::kOpus}},
      {.essence = "audio/webm", .codecs = {CodecId::kVorbis, CodecId::kOpus}},
      {.essence = "video/x-matroska",
       .codecs = {CodecId::kH264, CodecId::kHevc, CodecId::kVp8, CodecId::kVp9,
                  CodecId::kAv1, CodecId::kTheora, CodecId::kAac,
                  CodecId::kVorbis, CodecId::kOpus, CodecId::kAc3,
                  CodecId::kEac3, CodecId::kFlac, CodecId::kMp3}},
      {.essence = "audio/x-matroska",
       .codecs = {CodecId::kAac, CodecId::kVorbis, CodecId::kOpus,
                  CodecId::kAc3, CodecId::kEac3, CodecId::kFlac,
                  CodecId::kMp3}},
      {.essence = "audio/flac", .codecs = {CodecId::kFlac}},
      {.essence = "audio/mpeg", .codecs = {CodecId::kMp3}},
  }};
  return kContainers;
}

const Container* FindContainer(std::string_view essence) {
  for (const Container& entry : Containers()) {
    if (entry.essence == essence) {
      return &entry;
    }
  }
  return nullptr;
}

struct ParsedType {
  std::string essence;
  std::optional<std::string> codecs;
};

std::optional<ParsedType> ParseType(std::string_view type) {
  std::vector<std::string_view> parts = absl::StrSplit(type, ';');
  ParsedType parsed;
  parsed.essence = absl::AsciiStrToLower(absl::StripAsciiWhitespace(parts[0]));
  const size_t slash = parsed.essence.find('/');
  if (slash == std::string::npos || slash == 0 ||
      slash + 1 == parsed.essence.size() ||
      parsed.essence.find('/', slash + 1) != std::string::npos) {
    return std::nullopt;
  }
  for (size_t index = 1; index < parts.size(); ++index) {
    const std::string_view parameter = absl::StripAsciiWhitespace(parts[index]);
    if (parameter.empty()) {
      continue;
    }
    const size_t equals = parameter.find('=');
    const std::string name = absl::AsciiStrToLower(
        absl::StripAsciiWhitespace(parameter.substr(0, equals)));
    if (equals == std::string_view::npos) {
      if (name == "codecs" && !parsed.codecs.has_value()) {
        continue;
      }
      return std::nullopt;
    }
    if (name != "codecs" || parsed.codecs.has_value()) {
      return std::nullopt;
    }
    std::string_view value =
        absl::StripAsciiWhitespace(parameter.substr(equals + 1));
    if (value.empty()) {
      continue;
    }
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
        value.back() == value.front()) {
      value = value.substr(1, value.size() - 2);
    }
    parsed.codecs = std::string(value);
  }
  return parsed;
}

bool IsVideoCodec(CodecId id) {
  return id == CodecId::kH264 || id == CodecId::kHevc || id == CodecId::kVp9 ||
         id == CodecId::kVp8 || id == CodecId::kTheora || id == CodecId::kAv1;
}

penumbra::CapabilityRequest MakeRequest(const codec::CodecDescription& codec,
                                        std::string_view essence) {
  penumbra::CapabilityRequest request;
  const CodecId id = codec::CodecIdOf(codec);
  request.stream.codec = id;
  request.stream.codec_string = codec::FormatCodecString(codec);
  if (IsVideoCodec(id)) {
    request.stream.format = penumbra::VideoInfo{};
  } else {
    request.stream.format =
        penumbra::AudioInfo{.sample_rate = 48000,
                            .channel_count = 2,
                            .channel_mask = 3,
                            .sample_format = penumbra::SampleFormat::kF32};
  }
  request.mime_type = std::string(essence);
  return request;
}

}

CanPlayTypeResult AnswerCanPlayType(
    std::string_view type, const penumbra::CapabilityProvider* capabilities) {
  const std::optional<ParsedType> parsed = ParseType(type);
  if (!parsed.has_value()) {
    return CanPlayTypeResult::kNone;
  }
  const Container* container = FindContainer(parsed->essence);
  if (container == nullptr) {
    return CanPlayTypeResult::kNone;
  }
  if (!parsed->codecs.has_value()) {
    return CanPlayTypeResult::kMaybe;
  }
  const absl::StatusOr<std::vector<codec::CodecDescription>> list =
      codec::ParseCodecList(*parsed->codecs);
  if (!list.ok() || list->empty()) {
    return CanPlayTypeResult::kNone;
  }
  for (const codec::CodecDescription& description : *list) {
    const CodecId id = codec::CodecIdOf(description);
    if (std::ranges::find(container->codecs, id) == container->codecs.end()) {
      return CanPlayTypeResult::kNone;
    }
  }
  if (capabilities == nullptr) {
    return CanPlayTypeResult::kMaybe;
  }
  for (const codec::CodecDescription& description : *list) {
    const auto capability =
        capabilities->QueryDecoding(MakeRequest(description, parsed->essence));
    if (!capability.ok() || !capability->supported) {
      return CanPlayTypeResult::kNone;
    }
  }
  return CanPlayTypeResult::kProbably;
}

}
