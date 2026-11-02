#ifndef UMBRA_MANIFEST_URL_TEMPLATE_H_
#define UMBRA_MANIFEST_URL_TEMPLATE_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

namespace umbra::manifest {

struct TemplateValues {
  std::int64_t number = 0;
  std::int64_t time = 0;
  std::int64_t bandwidth = 0;
  std::string_view representation_id;
};

class UrlTemplate {
 public:
  static absl::StatusOr<UrlTemplate> Parse(std::string_view text);

  [[nodiscard]] std::string Expand(const TemplateValues& values) const;
  [[nodiscard]] bool UsesNumber() const { return uses_number_; }
  [[nodiscard]] bool UsesTime() const { return uses_time_; }
  [[nodiscard]] const std::string& Text() const { return text_; }

  friend bool operator==(const UrlTemplate& a, const UrlTemplate& b) {
    return a.text_ == b.text_;
  }

 private:
  enum class PartKind : std::uint8_t {
    kLiteral,
    kNumber,
    kTime,
    kBandwidth,
    kRepresentationId,
  };

  struct Part {
    PartKind kind = PartKind::kLiteral;
    std::string literal;
    int width = 1;
  };

  static absl::StatusOr<Part> ParseToken(std::string_view token);

  std::string text_;
  std::vector<Part> parts_;
  bool uses_number_ = false;
  bool uses_time_ = false;
};

}

#endif
