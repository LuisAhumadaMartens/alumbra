#ifndef PENUMBRA_DESKTOP_DECODER_TABLE_H_
#define PENUMBRA_DESKTOP_DECODER_TABLE_H_

#include <cstdint>
#include <span>
#include <string_view>

#include "penumbra/desktop/codec_string.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {

struct DecoderEntry {
  CodecId codec;
  std::string_view settings_name;
  std::string_view decoder_name;
  int64_t smooth_luma_samples_per_second;
  bool (*accepts)(const ParsedCodec& parsed);
};

std::span<const DecoderEntry> SoftwareDecoderTable();

const DecoderEntry* FindDecoderEntry(CodecId codec);

}

#endif
