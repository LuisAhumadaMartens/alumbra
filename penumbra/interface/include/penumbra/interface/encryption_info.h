#ifndef PENUMBRA_INTERFACE_ENCRYPTION_INFO_H_
#define PENUMBRA_INTERFACE_ENCRYPTION_INFO_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace penumbra {

enum class EncryptionScheme : uint8_t {
  kCenc,
  kCbcs,
};

struct SubsampleEntry {
  uint32_t clear_bytes = 0;
  uint32_t encrypted_bytes = 0;

  friend constexpr bool operator==(const SubsampleEntry&,
                                   const SubsampleEntry&) = default;
};

struct EncryptionPattern {
  uint8_t crypt_byte_block = 0;
  uint8_t skip_byte_block = 0;

  friend constexpr bool operator==(const EncryptionPattern&,
                                   const EncryptionPattern&) = default;
};

struct EncryptionInfo {
  EncryptionScheme scheme = EncryptionScheme::kCenc;
  std::array<uint8_t, 16> key_id{};
  std::vector<uint8_t> iv;
  std::vector<SubsampleEntry> subsamples;
  std::optional<EncryptionPattern> pattern;

  friend bool operator==(const EncryptionInfo&,
                         const EncryptionInfo&) = default;
};

enum class EncryptionCheck : uint8_t {
  kValid,
  kInvalidIvSize,
  kPatternWithoutCbcs,
  kInvalidPattern,
  kSubsampleSizeMismatch,
};

constexpr std::string_view EncryptionCheckName(EncryptionCheck check) {
  switch (check) {
    case EncryptionCheck::kValid:
      return "valid";
    case EncryptionCheck::kInvalidIvSize:
      return "invalid IV size";
    case EncryptionCheck::kPatternWithoutCbcs:
      return "pattern without cbcs";
    case EncryptionCheck::kInvalidPattern:
      return "invalid pattern";
    case EncryptionCheck::kSubsampleSizeMismatch:
      return "subsample sizes do not match the sample size";
  }
  return "unknown";
}

template <typename Sink>
void AbslStringify(Sink& sink, EncryptionCheck check) {
  sink.Append(EncryptionCheckName(check));
}

inline EncryptionCheck ValidateEncryption(const EncryptionInfo& info,
                                          size_t sample_size) {
  const bool cbcs = info.scheme == EncryptionScheme::kCbcs;
  if (cbcs ? info.iv.size() != 16
           : (info.iv.size() != 8 && info.iv.size() != 16)) {
    return EncryptionCheck::kInvalidIvSize;
  }
  if (info.pattern.has_value()) {
    if (!cbcs) {
      return EncryptionCheck::kPatternWithoutCbcs;
    }
    if (info.pattern->crypt_byte_block > 15 ||
        info.pattern->skip_byte_block > 15) {
      return EncryptionCheck::kInvalidPattern;
    }
  }
  if (!info.subsamples.empty()) {
    uint64_t total = 0;
    for (const SubsampleEntry& entry : info.subsamples) {
      total += entry.clear_bytes;
      total += entry.encrypted_bytes;
    }
    if (total != sample_size) {
      return EncryptionCheck::kSubsampleSizeMismatch;
    }
  }
  return EncryptionCheck::kValid;
}

}

#endif
