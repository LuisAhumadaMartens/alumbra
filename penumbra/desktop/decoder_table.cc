#include "penumbra/desktop/decoder_table.h"

#include <array>
#include <cstdint>
#include <span>

#include "penumbra/desktop/codec_string.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {
namespace {

constexpr int64_t kUhd60 = int64_t{3840} * 2160 * 60;

bool AcceptsAvc(const ParsedCodec& parsed) {
  switch (parsed.profile) {
    case -1:
    case 66:
    case 77:
    case 88:
    case 100:
    case 110:
    case 122:
    case 244:
      return true;
    default:
      return false;
  }
}

bool AcceptsHevc(const ParsedCodec& parsed) {
  return parsed.profile == -1 || (parsed.profile >= 1 && parsed.profile <= 4);
}

bool AcceptsVp9(const ParsedCodec& parsed) {
  if (parsed.profile == -1) {
    return true;
  }
  if (parsed.profile < 0 || parsed.profile > 3) {
    return false;
  }
  if (parsed.profile < 2) {
    return parsed.bit_depth == 8;
  }
  return parsed.bit_depth == 10 || parsed.bit_depth == 12;
}

bool AcceptsAv1(const ParsedCodec& parsed) {
  if (parsed.profile == -1) {
    return true;
  }
  if (parsed.profile < 0 || parsed.profile > 2) {
    return false;
  }
  if (parsed.bit_depth == 8 || parsed.bit_depth == 10) {
    return true;
  }
  return parsed.profile == 2 && parsed.bit_depth == 12;
}

bool AcceptsAny(const ParsedCodec& parsed) { return parsed.profile >= -1; }

constexpr std::array<DecoderEntry, 10> kTable = {{
    {.codec = CodecId::kH264,
     .settings_name = "h264",
     .decoder_name = "h264",
     .smooth_luma_samples_per_second = kUhd60,
     .accepts = AcceptsAvc},
    {.codec = CodecId::kHevc,
     .settings_name = "hevc",
     .decoder_name = "hevc",
     .smooth_luma_samples_per_second = kUhd60 / 2,
     .accepts = AcceptsHevc},
    {.codec = CodecId::kVp9,
     .settings_name = "vp9",
     .decoder_name = "vp9",
     .smooth_luma_samples_per_second = kUhd60 / 2,
     .accepts = AcceptsVp9},
    {.codec = CodecId::kAv1,
     .settings_name = "av1",
     .decoder_name = "dav1d",
     .smooth_luma_samples_per_second = kUhd60 / 4,
     .accepts = AcceptsAv1},
    {.codec = CodecId::kAac,
     .settings_name = "aac",
     .decoder_name = "aac",
     .smooth_luma_samples_per_second = 0,
     .accepts = AcceptsAny},
    {.codec = CodecId::kOpus,
     .settings_name = "opus",
     .decoder_name = "opus",
     .smooth_luma_samples_per_second = 0,
     .accepts = AcceptsAny},
    {.codec = CodecId::kAc3,
     .settings_name = "ac3",
     .decoder_name = "ac3",
     .smooth_luma_samples_per_second = 0,
     .accepts = AcceptsAny},
    {.codec = CodecId::kEac3,
     .settings_name = "eac3",
     .decoder_name = "eac3",
     .smooth_luma_samples_per_second = 0,
     .accepts = AcceptsAny},
    {.codec = CodecId::kFlac,
     .settings_name = "flac",
     .decoder_name = "flac",
     .smooth_luma_samples_per_second = 0,
     .accepts = AcceptsAny},
    {.codec = CodecId::kMp3,
     .settings_name = "mp3",
     .decoder_name = "mp3float",
     .smooth_luma_samples_per_second = 0,
     .accepts = AcceptsAny},
}};

}

std::span<const DecoderEntry> SoftwareDecoderTable() { return kTable; }

const DecoderEntry* FindDecoderEntry(CodecId codec) {
  for (const DecoderEntry& entry : kTable) {
    if (entry.codec == codec) {
      return &entry;
    }
  }
  return nullptr;
}

}
