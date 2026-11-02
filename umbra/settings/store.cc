#include "umbra/settings/store.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "umbra/base/lock.h"
#include "umbra/base/status.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/url_redactor.h"
#include "umbra/settings/platform.h"
#include "umbra/settings/setting_parser.h"
#include "umbra/settings/setting_types.h"
#include "umbra/settings/setting_value.h"

namespace umbra::settings {
namespace {

std::vector<Value> BuildDefaults(const Registry& registry) {
  std::vector<Value> defaults;
  defaults.reserve(registry.settings.size());
  for (const SettingInfo& info : registry.settings) {
    defaults.push_back(DefaultValue(info));
  }
  return defaults;
}

constexpr std::array<Layer, kLayerCount> kPrecedence{
    Layer::kSource, Layer::kUser, Layer::kIntegrator, Layer::kPortDefault};

std::size_t LayerIndex(Layer layer) { return static_cast<std::size_t>(layer); }

std::array<std::vector<std::optional<Value>>, kLayerCount> MakeLayers(
    std::size_t size) {
  std::array<std::vector<std::optional<Value>>, kLayerCount> layers;
  for (auto& layer : layers) {
    layer.assign(size, std::nullopt);
  }
  return layers;
}

}

Subscription& Subscription::operator=(Subscription&& other) noexcept {
  if (this != &other) {
    Cancel();
    state_ = std::move(other.state_);
  }
  return *this;
}

void Subscription::Cancel() {
  if (state_) {
    state_->active.store(false, std::memory_order_release);
    state_.reset();
  }
}

Store::Store(Registry registry, Platform platform)
    : Store(registry, platform, std::make_shared<const base::UrlRedactor>()) {}

Store::Store(Registry registry, Platform platform,
             std::shared_ptr<const base::UrlRedactor> redactor)
    : registry_(registry),
      platform_(platform),
      redactor_(std::move(redactor)),
      defaults_(BuildDefaults(registry)),
      layers_(MakeLayers(registry.settings.size())),
      user_locked_(registry.settings.size(), false),
      effective_(defaults_),
      observers_(registry.settings.size()) {}

Store::~Store() = default;

Value Store::GetValue(std::size_t index) const {
  const base::AutoLock lock(lock_);
  return effective_[index];
}

Snapshot Store::TakeSnapshot() const {
  const base::AutoLock lock(lock_);
  return Snapshot(std::make_shared<const std::vector<Value>>(effective_));
}

Value Store::Resolve(std::size_t index) const {
  for (const Layer layer : kPrecedence) {
    if (layer == Layer::kUser && user_locked_[index]) {
      continue;
    }
    const auto& stored = layers_.at(LayerIndex(layer))[index];
    if (stored.has_value()) {
      return *stored;
    }
  }
  return defaults_[index];
}

void Store::Refresh(std::size_t index) {
  Value resolved = Resolve(index);
  if (resolved == effective_[index]) {
    return;
  }
  effective_[index] = std::move(resolved);
  auto& observers = observers_[index];
  std::erase_if(observers, [](const auto& observer) {
    return !observer->active.load(std::memory_order_acquire);
  });
  for (const auto& observer : observers) {
    observer->runner->PostTask([observer, value = effective_[index]]() {
      if (observer->active.load(std::memory_order_acquire)) {
        observer->deliver(value);
      }
    });
  }
}

absl::Status Store::SetValue(Layer layer, std::size_t index, ValueType type,
                             std::optional<Value> value) {
  if (index >= registry_.settings.size()) {
    return absl::InvalidArgumentError("Unknown setting key");
  }
  const SettingInfo& info = registry_.settings[index];
  const std::string allowed = DescribeAllowed(info);
  if (info.type != type || !value.has_value()) {
    return RejectSetting(*redactor_, info.name, "",
                         "has a value that is not representable for this key",
                         allowed);
  }
  if (const auto violation = FindViolation(info, *value)) {
    return RejectSetting(*redactor_, info.name, FormatValue(*value), *violation,
                         allowed);
  }
  if (layer == Layer::kSource && info.apply == ApplyMode::kRestart) {
    return RejectSetting(*redactor_, info.name, FormatValue(*value),
                         "cannot be overridden per source", allowed);
  }
  const base::AutoLock lock(lock_);
  if (layer == Layer::kUser && user_locked_[index]) {
    return RejectSetting(*redactor_, info.name, FormatValue(*value),
                         "is locked by the integrator", allowed);
  }
  if (!info.platforms.Contains(platform_)) {
    return absl::OkStatus();
  }
  layers_.at(LayerIndex(layer))[index] = std::move(value);
  Refresh(index);
  return absl::OkStatus();
}

absl::Status Store::SetFromString(Layer layer, std::string_view key,
                                  std::string_view text) {
  const auto found = FindSetting(registry_, key);
  if (!found.has_value()) {
    return RejectSetting(*redactor_, key, text, "is not a known setting",
                         "a key from the settings schema");
  }
  const SettingInfo& info = registry_.settings[found->index];
  ALUMBRA_ASSIGN_OR_RETURN(Value value, ParseValue(info, text, *redactor_));
  return SetValue(layer, found->index, info.type, std::move(value));
}

absl::Status Store::SetFromAssignment(Layer layer,
                                      std::string_view assignment) {
  ALUMBRA_ASSIGN_OR_RETURN(const Assignment parsed,
                           ParseAssignment(assignment, *redactor_));
  return SetFromString(layer, parsed.key, parsed.value);
}

void Store::ResetValue(Layer layer, std::size_t index) {
  const base::AutoLock lock(lock_);
  layers_.at(LayerIndex(layer))[index].reset();
  Refresh(index);
}

void Store::Reset(Layer layer) {
  const base::AutoLock lock(lock_);
  for (std::size_t index = 0; index < registry_.settings.size(); ++index) {
    layers_.at(LayerIndex(layer))[index].reset();
    Refresh(index);
  }
}

void Store::SetLocked(std::size_t index, bool locked) {
  const base::AutoLock lock(lock_);
  user_locked_[index] = locked;
  Refresh(index);
}

absl::StatusOr<Subscription> Store::AddObserver(
    std::size_t index, ValueType type,
    std::shared_ptr<base::SequencedTaskRunner> runner,
    absl::AnyInvocable<void(const Value&)> deliver) {
  const SettingInfo& info = registry_.settings[index];
  if (info.type != type) {
    return absl::InvalidArgumentError(
        absl::StrCat(info.name, " has a different value type"));
  }
  if (info.apply != ApplyMode::kLive) {
    return absl::FailedPreconditionError(
        absl::StrCat(info.name, " is not live; read it from a snapshot"));
  }
  auto state = std::make_shared<internal::ObserverState>();
  state->runner = std::move(runner);
  state->deliver = std::move(deliver);
  const base::AutoLock lock(lock_);
  observers_[index].push_back(state);
  return Subscription(std::move(state));
}

}
