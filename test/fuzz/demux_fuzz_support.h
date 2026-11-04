#ifndef TEST_FUZZ_DEMUX_FUZZ_SUPPORT_H_
#define TEST_FUZZ_DEMUX_FUZZ_SUPPORT_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#if defined(ALUMBRA_USE_LIBFUZZER_PROVIDER) && \
    __has_include(<fuzzer/FuzzedDataProvider.h>)
#define ALUMBRA_HAS_LIBFUZZER_PROVIDER 1
#include <fuzzer/FuzzedDataProvider.h>
#else
#include <algorithm>
#endif

namespace alumbra::fuzz {

class FuzzedDataProvider {
 public:
  explicit FuzzedDataProvider(std::span<const std::uint8_t> remaining)
#ifdef ALUMBRA_HAS_LIBFUZZER_PROVIDER
      : provider_(remaining.data(), remaining.size()){}
#else
      : remaining_(remaining) {
  }
#endif

        std::vector<std::uint8_t> ConsumeBytes(std::size_t count) {
#ifdef ALUMBRA_HAS_LIBFUZZER_PROVIDER
    return provider_.ConsumeBytes<std::uint8_t>(count);
#else
    count = std::min(count, remaining_.size());
    std::vector<std::uint8_t> bytes;
    bytes.reserve(count);
    for (const std::uint8_t value : remaining_.first(count)) {
      bytes.push_back(value);
    }
    remaining_ = remaining_.subspan(count);
    return bytes;
#endif
  }

  std::vector<std::uint8_t> ConsumeRemainingBytes() {
#ifdef ALUMBRA_HAS_LIBFUZZER_PROVIDER
    return provider_.ConsumeRemainingBytes<std::uint8_t>();
#else
    return ConsumeBytes(remaining_.size());
#endif
  }

 private:
#ifdef ALUMBRA_HAS_LIBFUZZER_PROVIDER
  ::FuzzedDataProvider provider_;
#else
  std::span<const std::uint8_t> remaining_;
#endif
};

struct DemuxInput {
  std::uint8_t selector = 0;
  std::vector<std::uint8_t> file_bytes;
  std::vector<std::uint8_t> seek_operations;
};

inline DemuxInput ConsumeDemuxInput(const std::uint8_t* data,
                                    std::size_t size) {
  DemuxInput result;
  if (size == 0) {
    return result;
  }
  const std::span<const std::uint8_t> input(data, size);
  result.selector = input.front();
  if (size >= 7 && input[1] == 'A' && input[2] == 'L' && input[3] == '8' &&
      input[4] == '2') {
    const std::size_t requested = static_cast<std::size_t>(input[5]) |
                                  (static_cast<std::size_t>(input[6]) << 8U);
    FuzzedDataProvider provider(input.subspan(7));
    result.file_bytes = provider.ConsumeBytes(requested);
    result.seek_operations = provider.ConsumeRemainingBytes();
    if (result.seek_operations.size() > 16) {
      result.seek_operations.resize(16);
    }
    return result;
  }
  FuzzedDataProvider provider(input.subspan(1));
  result.file_bytes = provider.ConsumeRemainingBytes();
  return result;
}

}

#endif
