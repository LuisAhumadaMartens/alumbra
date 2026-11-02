#include "umbra/codec/codec_string.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/base/status.h"
#include "umbra/codec/reader_util.h"

namespace umbra::codec {
namespace {

constexpr std::size_t kHevcConstraintBytes = 6;
constexpr std::size_t kHevcMaxFields = 4 + kHevcConstraintBytes;
constexpr std::uint32_t kHevcMaxProfile = 31;
constexpr std::uint32_t kMaxAudioObjectType = 95;
constexpr std::uint8_t kMpeg4AudioOti = 0x40;

using Fields = std::vector<std::string_view>;

std::uint32_t ReverseBits(std::uint32_t value) {
  std::uint32_t result = 0;
  for (int i = 0; i < 32; ++i) {
    result = (result << 1U) | ((value >> i) & 1U);
  }
  return result;
}

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

absl::StatusOr<std::uint32_t> Decimal(std::string_view text,
                                      std::size_t min_digits,
                                      std::size_t max_digits,
                                      std::uint32_t limit) {
  if (text.size() < min_digits || text.size() > max_digits ||
      !std::ranges::all_of(text, IsDigit)) {
    return InvalidCodecString(absl::StrCat("bad decimal field '", text, "'"));
  }
  std::uint32_t value = 0;
  for (const char c : text) {
    value = (value * 10) + static_cast<std::uint32_t>(c - '0');
  }
  if (value > limit) {
    return InvalidCodecString(
        absl::StrCat("field '", text, "' exceeds ", limit));
  }
  return value;
}

absl::StatusOr<std::uint32_t> Hex(std::string_view text, std::size_t min_digits,
                                  std::size_t max_digits) {
  if (text.size() < min_digits || text.size() > max_digits) {
    return InvalidCodecString(absl::StrCat("bad hex field '", text, "'"));
  }
  std::uint32_t value = 0;
  for (const char c : text) {
    if (!absl::ascii_isxdigit(static_cast<unsigned char>(c))) {
      return InvalidCodecString(absl::StrCat("bad hex field '", text, "'"));
    }
    const std::uint32_t digit =
        IsDigit(c)
            ? static_cast<std::uint32_t>(c - '0')
            : static_cast<std::uint32_t>(absl::ascii_tolower(c) - 'a') + 10;
    value = (value << 4U) | digit;
  }
  return value;
}

template <typename T>
absl::Status Assign(T& target, const absl::StatusOr<std::uint32_t>& value) {
  if (!value.ok()) {
    return value.status();
  }
  target = static_cast<T>(*value);
  return absl::OkStatus();
}

absl::Status RequireFields(const Fields& fields,
                           std::initializer_list<std::size_t> counts) {
  if (std::ranges::find(counts, fields.size()) == counts.end()) {
    return InvalidCodecString(
        absl::StrCat("unexpected field count in '", fields.front(), "'"));
  }
  return absl::OkStatus();
}

absl::StatusOr<AvcCodec> ParseAvc(const Fields& fields) {
  ALUMBRA_RETURN_IF_ERROR(RequireFields(fields, {2}));
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint32_t packed, Hex(fields[1], 6, 6));
  AvcCodec codec;
  codec.sample_entry = std::string(fields[0]);
  codec.profile_idc = static_cast<std::uint8_t>(packed >> 16U);
  codec.constraint_flags = static_cast<std::uint8_t>(packed >> 8U);
  codec.level_idc = static_cast<std::uint8_t>(packed);
  return codec;
}

absl::Status ParseHevcProfile(std::string_view text, HevcCodec& codec) {
  if (!text.empty() && text.front() >= 'A' && text.front() <= 'C') {
    codec.profile_space = static_cast<std::uint8_t>(text.front() - 'A' + 1);
    text.remove_prefix(1);
  }
  return Assign(codec.profile_idc, Decimal(text, 1, 2, kHevcMaxProfile));
}

absl::Status ParseHevcTierLevel(std::string_view text, HevcCodec& codec) {
  if (text.empty() || (text.front() != 'L' && text.front() != 'H')) {
    return InvalidCodecString("HEVC tier must be L or H");
  }
  codec.high_tier = text.front() == 'H';
  text.remove_prefix(1);
  return Assign(codec.level_idc, Decimal(text, 1, 3, 255));
}

absl::StatusOr<HevcCodec> ParseHevc(const Fields& fields) {
  if (fields.size() < 4 || fields.size() > kHevcMaxFields) {
    return InvalidCodecString("unexpected field count in HEVC string");
  }
  HevcCodec codec;
  codec.sample_entry = std::string(fields[0]);
  ALUMBRA_RETURN_IF_ERROR(ParseHevcProfile(fields[1], codec));
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint32_t reversed, Hex(fields[2], 1, 8));
  codec.compatibility_flags = ReverseBits(reversed);
  ALUMBRA_RETURN_IF_ERROR(ParseHevcTierLevel(fields[3], codec));
  for (std::size_t i = 4; i < fields.size(); ++i) {
    ALUMBRA_RETURN_IF_ERROR(
        Assign(codec.constraint_flags.at(i - 4), Hex(fields[i], 1, 2)));
  }
  return codec;
}

bool ValidBitDepth(std::uint32_t depth) {
  return depth == 8 || depth == 10 || depth == 12;
}

absl::Status ParseVp9Optional(const Fields& fields, Vp9Codec& codec) {
  codec.has_optional_fields = true;
  ALUMBRA_RETURN_IF_ERROR(
      Assign(codec.chroma_subsampling, Decimal(fields[4], 2, 2, 3)));
  ALUMBRA_RETURN_IF_ERROR(
      Assign(codec.colour_primaries, Decimal(fields[5], 2, 3, 255)));
  ALUMBRA_RETURN_IF_ERROR(
      Assign(codec.transfer_characteristics, Decimal(fields[6], 2, 3, 255)));
  ALUMBRA_RETURN_IF_ERROR(
      Assign(codec.matrix_coefficients, Decimal(fields[7], 2, 3, 255)));
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint32_t range,
                           Decimal(fields[8], 2, 2, 1));
  codec.full_range = range == 1;
  return absl::OkStatus();
}

absl::StatusOr<Vp9Codec> ParseVp9(const Fields& fields) {
  ALUMBRA_RETURN_IF_ERROR(RequireFields(fields, {4, 9}));
  Vp9Codec codec;
  ALUMBRA_RETURN_IF_ERROR(Assign(codec.profile, Decimal(fields[1], 2, 2, 3)));
  ALUMBRA_RETURN_IF_ERROR(Assign(codec.level, Decimal(fields[2], 2, 3, 255)));
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint32_t depth,
                           Decimal(fields[3], 2, 2, 99));
  if (!ValidBitDepth(depth)) {
    return InvalidCodecString("VP9 bit depth must be 08, 10 or 12");
  }
  codec.bit_depth = static_cast<std::uint8_t>(depth);
  if (fields.size() == 9) {
    ALUMBRA_RETURN_IF_ERROR(ParseVp9Optional(fields, codec));
  }
  return codec;
}

absl::Status ParseAv1Subsampling(std::string_view text, Av1Codec& codec) {
  if (text.size() != 3 || !std::ranges::all_of(text, IsDigit)) {
    return InvalidCodecString("AV1 chroma subsampling needs three digits");
  }
  const int x = text[0] - '0';
  const int y = text[1] - '0';
  const int position = text[2] - '0';
  if (x > 1 || y > 1 || position > 3 || ((x == 0 || y == 0) && position != 0)) {
    return InvalidCodecString("AV1 chroma subsampling is out of range");
  }
  codec.subsampling_x = x == 1;
  codec.subsampling_y = y == 1;
  codec.chroma_sample_position = static_cast<std::uint8_t>(position);
  return absl::OkStatus();
}

absl::Status ParseAv1Optional(const Fields& fields, Av1Codec& codec) {
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint32_t mono,
                           Decimal(fields[4], 1, 1, 1));
  codec.monochrome = mono == 1;
  ALUMBRA_RETURN_IF_ERROR(ParseAv1Subsampling(fields[5], codec));
  ALUMBRA_RETURN_IF_ERROR(
      Assign(codec.colour_primaries, Decimal(fields[6], 2, 3, 255)));
  ALUMBRA_RETURN_IF_ERROR(
      Assign(codec.transfer_characteristics, Decimal(fields[7], 2, 3, 255)));
  ALUMBRA_RETURN_IF_ERROR(
      Assign(codec.matrix_coefficients, Decimal(fields[8], 2, 3, 255)));
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint32_t range,
                           Decimal(fields[9], 1, 1, 1));
  codec.full_range = range == 1;
  return absl::OkStatus();
}

absl::StatusOr<Av1Codec> ParseAv1(const Fields& fields) {
  ALUMBRA_RETURN_IF_ERROR(RequireFields(fields, {4, 10}));
  Av1Codec codec;
  ALUMBRA_RETURN_IF_ERROR(Assign(codec.profile, Decimal(fields[1], 1, 1, 2)));
  const std::string_view level = fields[2];
  if (level.size() != 3 || (level[2] != 'M' && level[2] != 'H')) {
    return InvalidCodecString("AV1 level and tier must look like 04M");
  }
  ALUMBRA_RETURN_IF_ERROR(
      Assign(codec.level, Decimal(level.substr(0, 2), 2, 2, 31)));
  codec.high_tier = level[2] == 'H';
  ALUMBRA_ASSIGN_OR_RETURN(const std::uint32_t depth,
                           Decimal(fields[3], 2, 2, 99));
  if (!ValidBitDepth(depth)) {
    return InvalidCodecString("AV1 bit depth must be 08, 10 or 12");
  }
  codec.bit_depth = static_cast<std::uint8_t>(depth);
  if (fields.size() == 10) {
    ALUMBRA_RETURN_IF_ERROR(ParseAv1Optional(fields, codec));
  }
  return codec;
}

absl::StatusOr<Mp4aCodec> ParseMp4a(const Fields& fields) {
  ALUMBRA_RETURN_IF_ERROR(RequireFields(fields, {2, 3}));
  Mp4aCodec codec;
  ALUMBRA_RETURN_IF_ERROR(
      Assign(codec.object_type_indication, Hex(fields[1], 2, 2)));
  if (fields.size() == 3) {
    ALUMBRA_ASSIGN_OR_RETURN(const std::uint32_t type,
                             Decimal(fields[2], 1, 2, kMaxAudioObjectType));
    codec.audio_object_type = static_cast<std::uint8_t>(type);
  }
  return codec;
}

absl::StatusOr<DolbyVisionCodec> ParseDolbyVision(const Fields& fields) {
  ALUMBRA_RETURN_IF_ERROR(RequireFields(fields, {3}));
  DolbyVisionCodec codec;
  codec.sample_entry = std::string(fields[0]);
  ALUMBRA_RETURN_IF_ERROR(Assign(codec.profile, Decimal(fields[1], 2, 2, 99)));
  ALUMBRA_RETURN_IF_ERROR(Assign(codec.level, Decimal(fields[2], 2, 3, 255)));
  return codec;
}

absl::StatusOr<CodecDescription> WithoutFields(const Fields& fields,
                                               CodecDescription codec) {
  ALUMBRA_RETURN_IF_ERROR(RequireFields(fields, {1}));
  return codec;
}

template <typename T>
absl::StatusOr<CodecDescription> Widen(absl::StatusOr<T> value) {
  if (!value.ok()) {
    return value.status();
  }
  return CodecDescription(*std::move(value));
}

bool IsOneOf(std::string_view name,
             std::initializer_list<std::string_view> names) {
  return std::ranges::find(names, name) != names.end();
}

std::string UpperHex(std::uint32_t value) {
  return absl::AsciiStrToUpper(absl::StrCat(absl::Hex(value)));
}

std::string TwoDigits(unsigned value) {
  return absl::StrCat(absl::Dec(value, absl::kZeroPad2));
}

std::string FormatHevc(const HevcCodec& codec) {
  std::string text = absl::StrCat(codec.sample_entry, ".");
  if (codec.profile_space > 0) {
    text.push_back(static_cast<char>('A' + codec.profile_space - 1));
  }
  absl::StrAppend(&text, static_cast<unsigned>(codec.profile_idc), ".",
                  UpperHex(ReverseBits(codec.compatibility_flags)), ".",
                  codec.high_tier ? "H" : "L",
                  static_cast<unsigned>(codec.level_idc));
  std::size_t used = kHevcConstraintBytes;
  while (used > 0 && codec.constraint_flags.at(used - 1) == 0) {
    --used;
  }
  for (std::size_t i = 0; i < used; ++i) {
    absl::StrAppend(&text, ".", UpperHex(codec.constraint_flags.at(i)));
  }
  return text;
}

std::string FormatVp9(const Vp9Codec& codec) {
  std::string text =
      absl::StrCat("vp09.", TwoDigits(codec.profile), ".",
                   TwoDigits(codec.level), ".", TwoDigits(codec.bit_depth));
  if (codec.has_optional_fields) {
    absl::StrAppend(&text, ".", TwoDigits(codec.chroma_subsampling), ".",
                    TwoDigits(codec.colour_primaries), ".",
                    TwoDigits(codec.transfer_characteristics), ".",
                    TwoDigits(codec.matrix_coefficients), ".",
                    TwoDigits(codec.full_range ? 1U : 0U));
  }
  return text;
}

bool HasDefaultAv1Tail(const Av1Codec& codec) {
  return !codec.monochrome && codec.subsampling_x && codec.subsampling_y &&
         codec.chroma_sample_position == 0 && codec.colour_primaries == 1 &&
         codec.transfer_characteristics == 1 &&
         codec.matrix_coefficients == 1 && !codec.full_range;
}

std::string FormatAv1(const Av1Codec& codec) {
  std::string text =
      absl::StrCat("av01.", static_cast<unsigned>(codec.profile), ".",
                   TwoDigits(codec.level), codec.high_tier ? "H" : "M", ".",
                   TwoDigits(codec.bit_depth));
  if (!HasDefaultAv1Tail(codec)) {
    const unsigned position = codec.subsampling_x && codec.subsampling_y
                                  ? codec.chroma_sample_position
                                  : 0U;
    absl::StrAppend(&text, ".", codec.monochrome ? 1 : 0, ".",
                    codec.subsampling_x ? 1 : 0, codec.subsampling_y ? 1 : 0,
                    position, ".", TwoDigits(codec.colour_primaries), ".",
                    TwoDigits(codec.transfer_characteristics), ".",
                    TwoDigits(codec.matrix_coefficients), ".",
                    codec.full_range ? 1 : 0);
  }
  return text;
}

std::string FormatMp4a(const Mp4aCodec& codec) {
  std::string text = absl::StrCat(
      "mp4a.", absl::AsciiStrToUpper(absl::StrCat(
                   absl::Hex(codec.object_type_indication, absl::kZeroPad2))));
  if (codec.audio_object_type.has_value()) {
    absl::StrAppend(&text, ".",
                    static_cast<unsigned>(*codec.audio_object_type));
  }
  return text;
}

penumbra::CodecId MpegAudioId(const Mp4aCodec& codec) {
  switch (codec.object_type_indication) {
    case kMpeg4AudioOti:
      return codec.audio_object_type == 34 ? penumbra::CodecId::kMp3
                                           : penumbra::CodecId::kAac;
    case 0x66:
    case 0x67:
    case 0x68:
      return penumbra::CodecId::kAac;
    case 0x69:
    case 0x6B:
      return penumbra::CodecId::kMp3;
    default:
      return penumbra::CodecId::kUnknown;
  }
}

penumbra::CodecId DolbyVisionId(const DolbyVisionCodec& codec) {
  if (codec.sample_entry == "dav1") {
    return penumbra::CodecId::kAv1;
  }
  if (codec.sample_entry == "dvav" || codec.sample_entry == "dva1") {
    return penumbra::CodecId::kH264;
  }
  return penumbra::CodecId::kHevc;
}

}

std::string FormatCodecString(const CodecDescription& codec) {
  struct Formatter {
    std::string operator()(const AvcCodec& avc) const {
      return absl::StrCat(avc.sample_entry, ".",
                          absl::Hex(avc.profile_idc, absl::kZeroPad2),
                          absl::Hex(avc.constraint_flags, absl::kZeroPad2),
                          absl::Hex(avc.level_idc, absl::kZeroPad2));
    }
    std::string operator()(const HevcCodec& hevc) const {
      return FormatHevc(hevc);
    }
    std::string operator()(const Vp9Codec& vp9) const { return FormatVp9(vp9); }
    std::string operator()(const Av1Codec& av1) const { return FormatAv1(av1); }
    std::string operator()(const Mp4aCodec& mp4a) const {
      return FormatMp4a(mp4a);
    }
    std::string operator()([[maybe_unused]] const OpusCodec& opus) const {
      return "opus";
    }
    std::string operator()([[maybe_unused]] const FlacCodec& flac) const {
      return "fLaC";
    }
    std::string operator()([[maybe_unused]] const Ac3Codec& ac3) const {
      return "ac-3";
    }
    std::string operator()([[maybe_unused]] const Eac3Codec& eac3) const {
      return "ec-3";
    }
    std::string operator()(const DolbyVisionCodec& dolby) const {
      return absl::StrCat(dolby.sample_entry, ".", TwoDigits(dolby.profile),
                          ".", TwoDigits(dolby.level));
    }
  };
  return std::visit(Formatter{}, codec);
}

absl::StatusOr<CodecDescription> ParseCodecString(std::string_view text) {
  if (text.empty()) {
    return InvalidCodecString("empty string");
  }
  const Fields fields = absl::StrSplit(text, '.');
  const std::string_view entry = fields.front();
  if (IsOneOf(entry, {"avc1", "avc3"})) {
    return Widen(ParseAvc(fields));
  }
  if (IsOneOf(entry, {"hvc1", "hev1"})) {
    return Widen(ParseHevc(fields));
  }
  if (entry == "vp09") {
    return Widen(ParseVp9(fields));
  }
  if (entry == "av01") {
    return Widen(ParseAv1(fields));
  }
  if (entry == "mp4a") {
    return Widen(ParseMp4a(fields));
  }
  if (IsOneOf(entry, {"dvh1", "dvhe", "dav1", "dvav", "dva1"})) {
    return Widen(ParseDolbyVision(fields));
  }
  if (IsOneOf(entry, {"opus", "Opus"})) {
    return WithoutFields(fields, OpusCodec{});
  }
  if (IsOneOf(entry, {"fLaC", "flac"})) {
    return WithoutFields(fields, FlacCodec{});
  }
  if (entry == "ac-3") {
    return WithoutFields(fields, Ac3Codec{});
  }
  if (entry == "ec-3") {
    return WithoutFields(fields, Eac3Codec{});
  }
  return Unsupported(absl::StrCat("codec '", entry, "'"));
}

absl::StatusOr<std::vector<CodecDescription>> ParseCodecList(
    std::string_view attribute) {
  std::vector<CodecDescription> codecs;
  std::string_view trimmed = absl::StripAsciiWhitespace(attribute);
  if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
    trimmed = absl::StripAsciiWhitespace(trimmed.substr(1, trimmed.size() - 2));
  }
  if (trimmed.empty()) {
    return codecs;
  }
  for (std::string_view item : absl::StrSplit(trimmed, ',')) {
    item = absl::StripAsciiWhitespace(item);
    ALUMBRA_ASSIGN_OR_RETURN(CodecDescription codec, ParseCodecString(item));
    codecs.push_back(std::move(codec));
  }
  return codecs;
}

penumbra::CodecId CodecIdOf(const CodecDescription& codec) {
  struct Identifier {
    penumbra::CodecId operator()([[maybe_unused]] const AvcCodec& avc) const {
      return penumbra::CodecId::kH264;
    }
    penumbra::CodecId operator()([[maybe_unused]] const HevcCodec& hevc) const {
      return penumbra::CodecId::kHevc;
    }
    penumbra::CodecId operator()([[maybe_unused]] const Vp9Codec& vp9) const {
      return penumbra::CodecId::kVp9;
    }
    penumbra::CodecId operator()([[maybe_unused]] const Av1Codec& av1) const {
      return penumbra::CodecId::kAv1;
    }
    penumbra::CodecId operator()(const Mp4aCodec& mp4a) const {
      return MpegAudioId(mp4a);
    }
    penumbra::CodecId operator()([[maybe_unused]] const OpusCodec& opus) const {
      return penumbra::CodecId::kOpus;
    }
    penumbra::CodecId operator()([[maybe_unused]] const FlacCodec& flac) const {
      return penumbra::CodecId::kFlac;
    }
    penumbra::CodecId operator()([[maybe_unused]] const Ac3Codec& ac3) const {
      return penumbra::CodecId::kAc3;
    }
    penumbra::CodecId operator()([[maybe_unused]] const Eac3Codec& eac3) const {
      return penumbra::CodecId::kEac3;
    }
    penumbra::CodecId operator()(const DolbyVisionCodec& dolby) const {
      return DolbyVisionId(dolby);
    }
  };
  return std::visit(Identifier{}, codec);
}

}
