#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "fuzz_target.h"
#include "umbra/source/url.h"

namespace {

void CheckNoDotSegments(std::string_view path) {
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t end = path.find('/', start);
    const std::string_view segment = path.substr(
        start,
        end == std::string_view::npos ? std::string_view::npos : end - start);
    if (segment == "." || segment == "..") {
      std::abort();
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
}

void CheckStable(const umbra::source::UrlReference& url) {
  const std::string text = url.ToString();
  const auto reparsed = umbra::source::UrlReference::Parse(text);
  if (!reparsed.ok() || reparsed->ToString() != text) {
    std::abort();
  }
}

}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const std::size_t split = input.find('\n');
  const std::string_view base_text = input.substr(0, split);
  const std::string_view reference_text = split == std::string_view::npos
                                              ? std::string_view()
                                              : input.substr(split + 1);

  const std::string cleaned = umbra::source::RemoveDotSegments(input);
  if (umbra::source::RemoveDotSegments(cleaned) != cleaned) {
    std::abort();
  }
  CheckNoDotSegments(cleaned);

  const auto base = umbra::source::UrlReference::Parse(base_text);
  const auto reference = umbra::source::UrlReference::Parse(reference_text);
  if (base.ok()) {
    CheckStable(*base);
    static_cast<void>(base->Origin());
    static_cast<void>(base->PathExtension());
    CheckStable(base->WithoutUserInfo());
  }
  if (reference.ok()) {
    CheckStable(*reference);
  }
  if (!base.ok() || !reference.ok() || !base->is_absolute()) {
    return 0;
  }
  const auto resolved = umbra::source::ResolveReference(*base, *reference);
  if (!resolved.ok() || !resolved->is_absolute()) {
    std::abort();
  }
  CheckStable(*resolved);
  if (reference->is_absolute() || reference->has_authority() ||
      !reference->path.empty()) {
    CheckNoDotSegments(resolved->path);
  }
  return 0;
}
