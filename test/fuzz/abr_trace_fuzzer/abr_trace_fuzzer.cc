#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <string>

#include "../../umbra/abr/simulator.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size > static_cast<std::size_t>(1024) * 1024) {
    return 0;
  }
  const std::string contents(reinterpret_cast<const char*>(data), size);
  for (const bool mahimahi : {false, true}) {
    try {
      static_cast<void>(
          umbra::abr::simulation::ReadTraceText("fuzz", contents, mahimahi));
    } catch (const std::exception&) {
      return 0;
    }
  }
  return 0;
}
