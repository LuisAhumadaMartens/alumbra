#include "penumbra/desktop/decode_errors.h"

#include <array>
#include <cerrno>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

extern "C" {
#include "libavutil/error.h"
}

namespace penumbra::desktop {

absl::Status CorruptSampleError(std::string_view what) {
  return absl::DataLossError(absl::StrCat("ALUMBRA-3001: ", what));
}

absl::Status CodecUnsupportedError(std::string_view what) {
  return absl::UnimplementedError(absl::StrCat("ALUMBRA-4001: ", what));
}

absl::Status DecoderInitializationError(std::string_view what) {
  return absl::InternalError(absl::StrCat("ALUMBRA-4003: ", what));
}

absl::Status DecodeFailedError(std::string_view what) {
  return absl::InternalError(std::string(what));
}

std::string AvErrorText(int code) {
  std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
  if (av_strerror(code, buffer.data(), buffer.size()) < 0) {
    return absl::StrCat("error ", code);
  }
  return buffer.data();
}

absl::Status StatusFromAvError(int code, std::string_view operation) {
  const std::string text = absl::StrCat(operation, ": ", AvErrorText(code));
  if (code == AVERROR_INVALIDDATA) {
    return CorruptSampleError(text);
  }
  if (code == AVERROR(ENOMEM)) {
    return absl::ResourceExhaustedError(text);
  }
  if (code == AVERROR_PATCHWELCOME || code == AVERROR(ENOSYS) ||
      code == AVERROR_DECODER_NOT_FOUND) {
    return CodecUnsupportedError(text);
  }
  return DecodeFailedError(text);
}

}
