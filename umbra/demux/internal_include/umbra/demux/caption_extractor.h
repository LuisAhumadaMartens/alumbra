#ifndef UMBRA_DEMUX_CAPTION_EXTRACTOR_H_
#define UMBRA_DEMUX_CAPTION_EXTRACTOR_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace umbra::demux {

enum class CaptionCodec : uint8_t {
  kH264,
  kHevc,
};

inline constexpr size_t kMaxCaptionMessagesPerSample = 64;

[[nodiscard]] std::vector<std::vector<uint8_t>> ExtractCaptionData(
    CaptionCodec codec, size_t nal_length_size,
    std::span<const uint8_t> sample);

[[nodiscard]] std::vector<uint8_t> ExtractHdrDynamicMetadata(
    CaptionCodec codec, size_t nal_length_size,
    std::span<const uint8_t> sample);

[[nodiscard]] std::vector<std::vector<uint8_t>> ExtractCaptionDataFromSei(
    std::span<const uint8_t> sei_rbsp);

}

#endif
