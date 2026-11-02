#include "penumbra/desktop/software_decoder_probe.h"

#include <string>
#include <string_view>

#include "absl/status/statusor.h"

extern "C" {
#include "dav1d/dav1d.h"
#include "libavcodec/codec.h"
}

namespace penumbra::desktop {

absl::StatusOr<bool> SoftwareDecoderProbe::IsCompiledIn(
    std::string_view decoder_name) const {
  if (decoder_name == "dav1d") {
    return dav1d_version() != nullptr;
  }
  const std::string name(decoder_name);
  const AVCodec* codec = avcodec_find_decoder_by_name(name.c_str());
  return codec != nullptr && av_codec_is_decoder(codec) != 0;
}

}
