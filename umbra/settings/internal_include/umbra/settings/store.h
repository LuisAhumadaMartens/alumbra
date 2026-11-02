#ifndef UMBRA_SETTINGS_STORE_H_
#define UMBRA_SETTINGS_STORE_H_

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "umbra/base/lock.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/url_redactor.h"
#include "umbra/settings/platform.h"
#include "umbra/settings/setting_types.h"

namespace umbra::settings {

enum class Layer {
  kPortDefault,
  kIntegrator,
  kUser,
  kSource,
};

inline constexpr std::size_t kLayerCount = 4;

namespace internal {

struct ObserverState {
  std::shared_ptr<base::SequencedTaskRunner> runner;
  absl::AnyInvocable<void(const Value&)> deliver;
  std::atomic<bool> active{true};
};

}

class Subscription {
 public:
  Subscription() = default;
  explicit Subscription(std::shared_ptr<internal::ObserverState> state)
      : state_(std::move(state)) {}
  Subscription(const Subscription&) = delete;
  Subscription& operator=(const Subscription&) = delete;
  Subscription(Subscription&& other) noexcept = default;
  Subscription& operator=(Subscription&& other) noexcept;
  ~Subscription() { Cancel(); }

  void Cancel();

 private:
  std::shared_ptr<internal::ObserverState> state_;
};

class Snapshot {
 public:
  Snapshot() = default;
  explicit Snapshot(std::shared_ptr<const std::vector<Value>> values)
      : values_(std::move(values)) {}

  template <typename T>
  [[nodiscard]] T Get(Key<T> key) const {
    return ValueTraits<T>::FromValue((*values_)[key.Index()]);
  }

 private:
  std::shared_ptr<const std::vector<Value>> values_;
};

class Store {
 public:
  Store(Registry registry, Platform platform);
  Store(Registry registry, Platform platform,
        std::shared_ptr<const base::UrlRedactor> redactor);
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;
  ~Store();

  template <typename T>
  [[nodiscard]] T Get(Key<T> key) const {
    return ValueTraits<T>::FromValue(GetValue(key.Index()));
  }

  template <typename T>
  [[nodiscard]] absl::Status Set(Layer layer, Key<T> key,
                                 std::type_identity_t<T> value) {
    return SetValue(layer, key.Index(), ValueTraits<T>::kType,
                    ValueTraits<T>::ToValue(std::move(value)));
  }

  [[nodiscard]] absl::Status SetFromString(Layer layer, std::string_view key,
                                           std::string_view text);

  [[nodiscard]] absl::Status SetFromAssignment(Layer layer,
                                               std::string_view assignment);

  template <typename T>
  void Reset(Layer layer, Key<T> key) {
    ResetValue(layer, key.Index());
  }

  void Reset(Layer layer);

  template <typename T>
  void SetUserLocked(Key<T> key, bool locked) {
    SetLocked(key.Index(), locked);
  }

  template <typename T>
  [[nodiscard]] bool IsSupported(Key<T> key) const {
    return registry_.settings[key.Index()].platforms.Contains(platform_);
  }

  template <typename T>
  [[nodiscard]] absl::StatusOr<Subscription> Observe(
      Key<T> key, std::shared_ptr<base::SequencedTaskRunner> runner,
      absl::AnyInvocable<void(const T&)> callback) {
    return AddObserver(
        key.Index(), ValueTraits<T>::kType, std::move(runner),
        [callback = std::move(callback)](const Value& value) mutable {
          callback(ValueTraits<T>::FromValue(value));
        });
  }

  [[nodiscard]] Snapshot TakeSnapshot() const;

  [[nodiscard]] const Registry& Schema() const { return registry_; }

 private:
  [[nodiscard]] Value GetValue(std::size_t index) const;
  [[nodiscard]] absl::Status SetValue(Layer layer, std::size_t index,
                                      ValueType type,
                                      std::optional<Value> value);
  void ResetValue(Layer layer, std::size_t index);
  void SetLocked(std::size_t index, bool locked);
  [[nodiscard]] absl::StatusOr<Subscription> AddObserver(
      std::size_t index, ValueType type,
      std::shared_ptr<base::SequencedTaskRunner> runner,
      absl::AnyInvocable<void(const Value&)> deliver);
  [[nodiscard]] Value Resolve(std::size_t index) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);
  void Refresh(std::size_t index) ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  const Registry registry_;
  const Platform platform_;
  const std::shared_ptr<const base::UrlRedactor> redactor_;
  const std::vector<Value> defaults_;
  mutable base::Lock lock_;
  std::array<std::vector<std::optional<Value>>, kLayerCount> layers_
      ABSL_GUARDED_BY(lock_);
  std::vector<bool> user_locked_ ABSL_GUARDED_BY(lock_);
  std::vector<Value> effective_ ABSL_GUARDED_BY(lock_);
  std::vector<std::vector<std::shared_ptr<internal::ObserverState>>> observers_
      ABSL_GUARDED_BY(lock_);
};

}

#endif
