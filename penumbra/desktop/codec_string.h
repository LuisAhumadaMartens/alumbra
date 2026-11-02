#ifndef PENUMBRA_DESKTOP_CODEC_STRING_H_
#define PENUMBRA_DESKTOP_CODEC_STRING_H_

#include <optional>
#include <string_view>

#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {

struct ParsedCodec {
  CodecId codec = CodecId::kUnknown;
  int profile = -1;
  int level = -1;
  int bit_depth = 0;
};

std::optional<ParsedCodec> ParseCodecString(std::string_view codec_string);

}

#endif
