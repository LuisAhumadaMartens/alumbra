#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "umbra/manifest/dash_mpd_parser.h"

namespace {

void AppendEscaped(std::string_view input, std::string& output) {
  for (const char value : input) {
    switch (value) {
      case '&':
        output.append("&amp;");
        break;
      case '<':
        output.append("&lt;");
        break;
      case '"':
        output.append("&quot;");
        break;
      case '\'':
        output.append("&apos;");
        break;
      default:
        output.push_back(value);
        break;
    }
  }
}

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::size_t split = size / 2;
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const std::string_view duration = input.substr(0, split);
  const std::string_view date_time = input.substr(split);
  std::string mpd =
      R"(<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="static" )";
  if (!duration.empty()) {
    mpd.append("mediaPresentationDuration=\"");
    AppendEscaped(duration, mpd);
    mpd.append("\" ");
  }
  if (!date_time.empty()) {
    mpd.append("availabilityStartTime=\"");
    AppendEscaped(date_time, mpd);
    mpd.append("\" ");
  }
  mpd.append(R"(minBufferTime="PT1S"><Period id="p" )");
  if (!duration.empty()) {
    mpd.append("duration=\"");
    AppendEscaped(duration, mpd);
    mpd.append("\" ");
  }
  mpd.append("/></MPD>");
  static_cast<void>(umbra::manifest::ParseDashMpd(mpd));
  return 0;
}
