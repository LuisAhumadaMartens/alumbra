#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

#include "fuzz_target.h"
#include "umbra/source/source_type.h"

namespace {

constexpr std::array<std::string_view, 8> kExtensions = {
    "", "m3u8", "mpd", "mp4", "webm", "ts", "flv", "txt"};
constexpr std::array<std::string_view, 8> kContentTypes = {
    "",
    "application/vnd.apple.mpegurl",
    "application/dash+xml",
    "video/mp4",
    "video/webm",
    "video/mp2t",
    "video/x-flv",
    "text/plain; charset=utf-8"};

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size == 0) {
    return 0;
  }
  const std::span<const std::uint8_t> input(data, size);
  const std::uint8_t flags = input.front();
  const bool end_of_source = (flags & 1U) != 0;
  const umbra::source::SniffHints hints{
      .extension =
          std::string(kExtensions.at((flags >> 1U) % kExtensions.size())),
      .content_type =
          std::string(kContentTypes.at((flags >> 4U) % kContentTypes.size()))};
  const std::span<const std::uint8_t> bytes = input.subspan(1);

  const auto result =
      umbra::source::SniffSourceType(bytes, end_of_source, hints);
  using umbra::source::SniffOutcome;
  if (result.outcome == SniffOutcome::kNeedMoreBytes) {
    if (end_of_source || result.wanted_bytes <= bytes.size() ||
        result.wanted_bytes > umbra::source::kMaxSniffBytes) {
      std::abort();
    }
  }
  if ((result.outcome == SniffOutcome::kDecided) != result.type.has_value()) {
    std::abort();
  }
  if (result.outcome == SniffOutcome::kDecided &&
      result.signal == umbra::source::TypeSignal::kSignature) {
    const auto bare = umbra::source::SniffSourceType(bytes, end_of_source, {});
    if (bare.outcome != SniffOutcome::kDecided || bare.type != result.type) {
      std::abort();
    }
  }
  return 0;
}
