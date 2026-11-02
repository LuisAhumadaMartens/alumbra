#ifndef UMBRA_SOURCE_SOURCE_TYPE_H_
#define UMBRA_SOURCE_SOURCE_TYPE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace umbra::source {

enum class SourceType { kHls, kDash, kMp4, kMatroska, kMpegTs, kFlv };

enum class TypeSignal { kExplicitType, kSignature, kExtension, kContentType };

inline constexpr size_t kSniffBytes = 4096;
inline constexpr size_t kMaxSniffBytes = 65536;

[[nodiscard]] std::string_view SourceTypeName(SourceType type);
[[nodiscard]] std::string_view TypeSignalName(TypeSignal signal);
[[nodiscard]] std::optional<SourceType> ParseSourceType(std::string_view name);
[[nodiscard]] bool IsAdaptive(SourceType type);

struct SniffHints {
  std::string extension;
  std::string content_type;
};

enum class SniffOutcome { kDecided, kNeedMoreBytes, kUndecided };

struct SniffResult {
  SniffOutcome outcome = SniffOutcome::kUndecided;
  std::optional<SourceType> type = std::nullopt;
  TypeSignal signal = TypeSignal::kSignature;
  size_t wanted_bytes = 0;
};

[[nodiscard]] SniffResult SniffSourceType(std::span<const uint8_t> bytes,
                                          bool end_of_source,
                                          const SniffHints& hints);

}

#endif
