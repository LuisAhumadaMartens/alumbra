#ifndef UMBRA_OBSERVABILITY_LOG_H_
#define UMBRA_OBSERVABILITY_LOG_H_

#include <atomic>
#include <concepts>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "umbra/base/url_redactor.h"
#include "umbra/observability/category.h"

#ifndef ALUMBRA_LOG_FLOOR_LEVEL
#define ALUMBRA_LOG_FLOOR_LEVEL 0
#endif

namespace umbra::observability {

using FieldValue =
    std::variant<std::int64_t, std::uint64_t, double, bool, std::string>;

struct LogField {
  std::string name;
  FieldValue value;
};

struct LogRecord {
  Category category;
  Level level;
  std::string message;
  std::vector<LogField> fields;
  std::uint64_t sequence;
  std::uint32_t thread;
};

class Sink {
 public:
  virtual ~Sink() = default;
  virtual void Write(const LogRecord& record) = 0;
};

namespace internal {

inline constexpr std::int64_t kNeverLoggedMs =
    std::numeric_limits<std::int64_t>::min();

[[nodiscard]] constexpr std::uint32_t PackLogState(Level level,
                                                   std::uint32_t categories) {
  return (static_cast<std::uint32_t>(level) << 16) |
         (categories & kAllCategories);
}

[[nodiscard]] inline std::atomic<std::uint32_t>& LogState() {
  static std::atomic<std::uint32_t> state{
      PackLogState(Level::kInfo, kAllCategories)};
  return state;
}

void Emit(Category category, Level level, const std::string& message,
          std::vector<LogField> fields);

[[nodiscard]] bool IsLowerSnakeCase(std::string_view name);
[[nodiscard]] std::int64_t NowMs();

[[nodiscard]] inline bool EveryN(std::atomic<std::uint64_t>& counter,
                                 std::uint64_t n) {
  return counter.fetch_add(1, std::memory_order_relaxed) % (n == 0 ? 1 : n) ==
         0;
}

[[nodiscard]] bool EveryMs(std::atomic<std::int64_t>& last_ms,
                           std::int64_t interval_ms);

}

template <int kFloor>
[[nodiscard]] inline bool LogEnabled(Category category, Level level) {
  if (static_cast<int>(level) < kFloor) {
    return false;
  }
  const std::uint32_t state =
      internal::LogState().load(std::memory_order_relaxed);
  return static_cast<std::uint32_t>(level) >= (state >> 16) &&
         (state & CategoryBit(category)) != 0;
}

void SetLogLevel(Level level);
void SetLogCategoryMask(std::uint32_t mask);
[[nodiscard]] Level GetLogLevel();
[[nodiscard]] std::uint32_t GetLogCategoryMask();
void SetSink(std::shared_ptr<Sink> sink);
void SetRedactor(std::shared_ptr<const base::UrlRedactor> redactor);

class LogMessage {
 public:
  LogMessage(Category category, Level level)
      : category_(category), level_(level) {}
  LogMessage(const LogMessage&) = delete;
  LogMessage& operator=(const LogMessage&) = delete;
  ~LogMessage() {
    internal::Emit(category_, level_, text_, std::move(fields_));
  }

  LogMessage& Field(std::string_view name, std::string_view value) {
    fields_.push_back({std::string(name), std::string(value)});
    return *this;
  }
  template <typename T>
    requires std::same_as<T, bool>
  LogMessage& Field(std::string_view name, T value) {
    fields_.push_back({std::string(name), value});
    return *this;
  }
  template <std::signed_integral T>
  LogMessage& Field(std::string_view name, T value) {
    fields_.push_back({std::string(name), static_cast<std::int64_t>(value)});
    return *this;
  }
  template <std::unsigned_integral T>
    requires(!std::same_as<T, bool>)
  LogMessage& Field(std::string_view name, T value) {
    fields_.push_back({std::string(name), static_cast<std::uint64_t>(value)});
    return *this;
  }
  template <std::floating_point T>
  LogMessage& Field(std::string_view name, T value) {
    fields_.push_back({std::string(name), static_cast<double>(value)});
    return *this;
  }

  LogMessage& operator<<(std::string_view text) {
    text_.append(text);
    return *this;
  }
  template <typename T>
    requires std::same_as<T, bool>
  LogMessage& operator<<(T value) {
    text_.append(value ? "true" : "false");
    return *this;
  }
  template <std::integral T>
    requires(!std::same_as<T, bool>)
  LogMessage& operator<<(T value) {
    text_.append(std::to_string(value));
    return *this;
  }
  template <std::floating_point T>
  LogMessage& operator<<(T value) {
    text_.append(std::to_string(value));
    return *this;
  }

 private:
  Category category_;
  Level level_;
  std::string text_;
  std::vector<LogField> fields_;
};

}

#define ALUMBRA_LOG(category, level)                                \
  if (!::umbra::observability::LogEnabled<ALUMBRA_LOG_FLOOR_LEVEL>( \
          ::umbra::observability::Category::category,               \
          ::umbra::observability::Level::level)) {                  \
  } else                                                            \
    ::umbra::observability::LogMessage(                             \
        ::umbra::observability::Category::category,                 \
        ::umbra::observability::Level::level)

#define ALUMBRA_LOG_EVERY_N(category, level, n)                     \
  if (!::umbra::observability::LogEnabled<ALUMBRA_LOG_FLOOR_LEVEL>( \
          ::umbra::observability::Category::category,               \
          ::umbra::observability::Level::level) ||                  \
      ![&] {                                                        \
        static std::atomic<std::uint64_t> alumbra_log_counter{0};   \
        return ::umbra::observability::internal::EveryN(            \
            alumbra_log_counter, static_cast<std::uint64_t>(n));    \
      }()) {                                                        \
  } else                                                            \
    ::umbra::observability::LogMessage(                             \
        ::umbra::observability::Category::category,                 \
        ::umbra::observability::Level::level)

#define ALUMBRA_LOG_EVERY_MS(category, level, interval_ms)                \
  if (!::umbra::observability::LogEnabled<ALUMBRA_LOG_FLOOR_LEVEL>(       \
          ::umbra::observability::Category::category,                     \
          ::umbra::observability::Level::level) ||                        \
      ![&] {                                                              \
        static std::atomic<std::int64_t> alumbra_log_last_ms{             \
            ::umbra::observability::internal::kNeverLoggedMs};            \
        return ::umbra::observability::internal::EveryMs(                 \
            alumbra_log_last_ms, static_cast<std::int64_t>(interval_ms)); \
      }()) {                                                              \
  } else                                                                  \
    ::umbra::observability::LogMessage(                                   \
        ::umbra::observability::Category::category,                       \
        ::umbra::observability::Level::level)

#endif
