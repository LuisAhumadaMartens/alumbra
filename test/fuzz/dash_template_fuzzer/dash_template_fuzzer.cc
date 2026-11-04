#include <cstddef>
#include <cstdint>
#include <string_view>

#include "umbra/manifest/url_template.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const auto url_template = umbra::manifest::UrlTemplate::Parse(input);
  if (url_template.ok()) {
    const umbra::manifest::TemplateValues values{
        .number = static_cast<std::int64_t>(size),
        .time = static_cast<std::int64_t>(size) * 17,
        .bandwidth = static_cast<std::int64_t>(size) * 31,
        .representation_id = input};
    static_cast<void>(url_template->Expand(values));
  }
  return 0;
}
