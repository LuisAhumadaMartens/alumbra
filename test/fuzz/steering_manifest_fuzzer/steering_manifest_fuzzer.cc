#include <cstddef>
#include <cstdint>
#include <span>

#include "penumbra/interface/shared_bytes.h"
#include "umbra/fetch/content_steering.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const auto bytes = penumbra::SharedBytes::Copy(std::span(data, size));
  umbra::fetch::JsonSteeringParser parser;
  static_cast<void>(parser.Parse(bytes));
  return 0;
}
