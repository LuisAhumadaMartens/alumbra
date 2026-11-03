#include "umbra/ads/obstruction_registry.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>
#include <vector>

#include "absl/status/status.h"

namespace umbra::ads {
namespace {

bool IsReasonCharacter(char character) {
  return (character >= 'A' && character <= 'Z') ||
         (character >= 'a' && character <= 'z') ||
         (character >= '0' && character <= '9') || character == ' ';
}

bool IsValidRectangle(const Rectangle& rectangle) {
  return std::isfinite(rectangle.x) && std::isfinite(rectangle.y) &&
         std::isfinite(rectangle.width) && std::isfinite(rectangle.height) &&
         rectangle.width > 0 && rectangle.height > 0;
}

}

ObstructionRegistry::ObstructionRegistry(ProviderMode mode,
                                         ObstructionProvider* provider)
    : mode_(mode), provider_(provider) {}

absl::Status ObstructionRegistry::DeclareView(ObstructionEntry entry) {
  if (BreakActive()) {
    return absl::FailedPreconditionError(
        "views cannot be declared during an ad break");
  }
  if (entry.view == 0) {
    return absl::InvalidArgumentError("view handle must be nonzero");
  }
  if (entry.reason.empty() || entry.reason.size() > 50 ||
      !std::ranges::all_of(entry.reason, IsReasonCharacter)) {
    return absl::InvalidArgumentError(
        "reason must contain 1 to 50 letters, digits, or spaces");
  }
  if (!IsValidRectangle(entry.bounds)) {
    return absl::InvalidArgumentError(
        "view bounds must have finite positive size");
  }
  if (std::ranges::any_of(entries_, [&entry](const ObstructionEntry& existing) {
        return existing.view == entry.view;
      })) {
    return absl::AlreadyExistsError("view handle is already declared");
  }
  entries_.push_back(std::move(entry));
  return absl::OkStatus();
}

absl::Status ObstructionRegistry::RemoveView(ViewHandle view) {
  if (BreakActive()) {
    return absl::FailedPreconditionError(
        "views cannot be removed during an ad break");
  }
  const auto entry =
      std::ranges::find_if(entries_, [view](const ObstructionEntry& candidate) {
        return candidate.view == view;
      });
  if (entry == entries_.end()) {
    return absl::NotFoundError("view handle is not declared");
  }
  entries_.erase(entry);
  return absl::OkStatus();
}

absl::Status ObstructionRegistry::BeginBreak(Rectangle ad_container) {
  if (BreakActive()) {
    return absl::FailedPreconditionError("an ad break is already active");
  }
  if (!IsValidRectangle(ad_container)) {
    return absl::InvalidArgumentError(
        "ad container bounds must have finite positive size");
  }
  for (const ObstructionEntry& entry : entries_) {
    const absl::Status status = ValidateEntry(entry);
    if (!status.ok()) {
      return status;
    }
  }
  absl::Status break_status = ValidateBreak(ad_container);
  if (!break_status.ok()) {
    return break_status;
  }
  if (mode_ == ProviderMode::kForward && provider_ == nullptr) {
    return absl::FailedPreconditionError(
        "forward mode requires an obstruction provider");
  }
  ad_container_ = ad_container;
  if (mode_ != ProviderMode::kForward) {
    return absl::OkStatus();
  }
  for (const ObstructionEntry& entry : entries_) {
    if (entry.role == ViewRole::kNeverShownOverAds) {
      continue;
    }
    const absl::Status status = provider_->RegisterFriendlyObstruction(entry);
    if (!status.ok()) {
      for (const ViewHandle view : std::views::reverse(forwarded_)) {
        static_cast<void>(provider_->UnregisterFriendlyObstruction(view));
      }
      forwarded_.clear();
      ad_container_.reset();
      return status;
    }
    forwarded_.push_back(entry.view);
  }
  return absl::OkStatus();
}

absl::Status ObstructionRegistry::EndBreak() {
  if (!BreakActive()) {
    return absl::FailedPreconditionError("no ad break is active");
  }
  absl::Status first_error = absl::OkStatus();
  if (mode_ == ProviderMode::kForward) {
    for (const ViewHandle view : forwarded_) {
      const absl::Status status =
          provider_->UnregisterFriendlyObstruction(view);
      if (first_error.ok() && !status.ok()) {
        first_error = status;
      }
    }
  }
  forwarded_.clear();
  ad_container_.reset();
  return first_error;
}

bool ObstructionRegistry::BreakActive() const {
  return ad_container_.has_value();
}

std::vector<DebugRegion> ObstructionRegistry::DebugRegions() const {
  std::vector<DebugRegion> regions;
  regions.reserve(entries_.size() + (ad_container_.has_value() ? 1 : 0));
  for (const ObstructionEntry& entry : entries_) {
    regions.push_back(
        DebugRegion{.view = entry.view,
                    .role = entry.role,
                    .purpose = entry.purpose,
                    .bounds = entry.bounds,
                    .forbidden = entry.role == ViewRole::kNeverShownOverAds});
  }
  if (ad_container_.has_value()) {
    regions.push_back(DebugRegion{.view = 0,
                                  .role = ViewRole::kNeverShownOverAds,
                                  .purpose = ObstructionPurpose::kUnspecified,
                                  .bounds = *ad_container_,
                                  .forbidden = true});
  }
  return regions;
}

absl::Status ObstructionRegistry::ValidateEntry(const ObstructionEntry& entry) {
  if (entry.role == ViewRole::kUnspecified) {
    return absl::InvalidArgumentError(
        "every view in an ad break must have a declared role");
  }
  if (entry.role != ViewRole::kFriendlyObstruction &&
      entry.role != ViewRole::kNeverShownOverAds) {
    return absl::InvalidArgumentError("view role is invalid");
  }
  if (entry.role == ViewRole::kFriendlyObstruction &&
      (entry.purpose < ObstructionPurpose::kMediaControls ||
       entry.purpose > ObstructionPurpose::kOther)) {
    return absl::InvalidArgumentError(
        "friendly obstruction must have a declared purpose");
  }
  if (entry.role == ViewRole::kNeverShownOverAds &&
      entry.purpose != ObstructionPurpose::kUnspecified) {
    return absl::InvalidArgumentError(
        "never shown over ads views do not have a friendly purpose");
  }
  return absl::OkStatus();
}

absl::Status ObstructionRegistry::ValidateBreak(Rectangle ad_container) const {
  if (mode_ != ProviderMode::kEnforceNoOverlap) {
    return absl::OkStatus();
  }
  for (const ObstructionEntry& entry : entries_) {
    if (Intersects(entry.bounds, ad_container)) {
      return absl::FailedPreconditionError(
          "a declared view overlaps the ad container");
    }
  }
  return absl::OkStatus();
}

bool ObstructionRegistry::Intersects(const Rectangle& first,
                                     const Rectangle& second) {
  return first.x < second.x + second.width &&
         first.x + first.width > second.x &&
         first.y < second.y + second.height &&
         first.y + first.height > second.y;
}

}
