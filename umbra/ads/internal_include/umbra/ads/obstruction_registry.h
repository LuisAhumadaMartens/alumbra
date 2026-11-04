#ifndef UMBRA_ADS_OBSTRUCTION_REGISTRY_H_
#define UMBRA_ADS_OBSTRUCTION_REGISTRY_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"

namespace umbra::ads {

using ViewHandle = std::uint64_t;

enum class ObstructionPurpose {
  kUnspecified,
  kMediaControls,
  kCloseAd,
  kNotVisible,
  kOther
};

enum class ViewRole { kUnspecified, kFriendlyObstruction, kNeverShownOverAds };

enum class ProviderMode { kForward, kEnforceNoOverlap, kRecordOnly };

struct Rectangle {
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
  friend bool operator==(const Rectangle&, const Rectangle&) = default;
};

struct ObstructionEntry {
  ViewHandle view = 0;
  ViewRole role = ViewRole::kUnspecified;
  ObstructionPurpose purpose = ObstructionPurpose::kUnspecified;
  std::string reason;
  Rectangle bounds;
  friend bool operator==(const ObstructionEntry&,
                         const ObstructionEntry&) = default;
};

struct DebugRegion {
  ViewHandle view = 0;
  ViewRole role = ViewRole::kUnspecified;
  ObstructionPurpose purpose = ObstructionPurpose::kUnspecified;
  Rectangle bounds;
  bool forbidden = false;
  friend bool operator==(const DebugRegion&, const DebugRegion&) = default;
};

class ObstructionProvider {
 public:
  virtual ~ObstructionProvider() = default;
  [[nodiscard]] virtual absl::Status RegisterFriendlyObstruction(
      const ObstructionEntry& entry) = 0;
  [[nodiscard]] virtual absl::Status UnregisterFriendlyObstruction(
      ViewHandle view) = 0;
};

class ObstructionRegistry {
 public:
  ObstructionRegistry(ProviderMode mode, ObstructionProvider* provider);

  [[nodiscard]] absl::Status DeclareView(ObstructionEntry entry);
  [[nodiscard]] absl::Status RemoveView(ViewHandle view);
  [[nodiscard]] absl::Status BeginBreak(Rectangle ad_container);
  [[nodiscard]] absl::Status EndBreak();
  [[nodiscard]] bool BreakActive() const;
  [[nodiscard]] std::vector<DebugRegion> DebugRegions() const;

 private:
  [[nodiscard]] static absl::Status ValidateEntry(
      const ObstructionEntry& entry);
  [[nodiscard]] absl::Status ValidateBreak(Rectangle ad_container) const;
  [[nodiscard]] static bool Intersects(const Rectangle& first,
                                       const Rectangle& second);

  ProviderMode mode_;
  ObstructionProvider* provider_;
  std::vector<ObstructionEntry> entries_;
  std::vector<ViewHandle> forwarded_;
  std::optional<Rectangle> ad_container_;
};

}

#endif
