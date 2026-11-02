#include "umbra/observability/log.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "umbra/base/lock.h"
#include "umbra/base/url_redactor.h"
#include "umbra/observability/category.h"

namespace umbra::observability {
namespace {

struct Config {
  base::Lock lock;
  std::shared_ptr<Sink> sink ABSL_GUARDED_BY(lock);
  std::shared_ptr<const base::UrlRedactor> redactor ABSL_GUARDED_BY(lock);
};

Config& GetConfig() {
  static Config config;
  return config;
}

std::uint32_t CurrentThread() {
  static std::atomic<std::uint32_t> next_thread{0};
  thread_local const std::uint32_t id =
      next_thread.fetch_add(1, std::memory_order_relaxed) + 1;
  return id;
}

}

namespace internal {

bool IsLowerSnakeCase(std::string_view name) {
  if (name.empty() || name.front() < 'a' || name.front() > 'z' ||
      name.back() == '_') {
    return false;
  }
  char previous = 0;
  for (const char ch : name) {
    const bool valid =
        (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
    if (!valid || (ch == '_' && previous == '_')) {
      return false;
    }
    previous = ch;
  }
  return true;
}

std::int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

bool EveryMs(std::atomic<std::int64_t>& last_ms, std::int64_t interval_ms) {
  const std::int64_t now = NowMs();
  std::int64_t last = last_ms.load(std::memory_order_relaxed);
  while (last == kNeverLoggedMs || now - last >= interval_ms) {
    if (last_ms.compare_exchange_weak(last, now, std::memory_order_relaxed)) {
      return true;
    }
  }
  return false;
}

void Emit(Category category, Level level, const std::string& message,
          std::vector<LogField> fields) {
  static std::atomic<std::uint64_t> next_sequence{0};
  Config& config = GetConfig();
  std::shared_ptr<Sink> sink;
  std::shared_ptr<const base::UrlRedactor> redactor;
  {
    const base::AutoLock lock(config.lock);
    sink = config.sink;
    redactor = config.redactor;
  }
  if (!sink) {
    return;
  }
  static const base::UrlRedactor kDefaultRedactor;
  const base::UrlRedactor& active = redactor ? *redactor : kDefaultRedactor;
  LogRecord record{
      .category = category,
      .level = level,
      .message = active.RedactText(message),
      .fields = {},
      .sequence = next_sequence.fetch_add(1, std::memory_order_relaxed),
      .thread = CurrentThread()};
  record.fields.reserve(fields.size());
  for (LogField& field : fields) {
    std::string name = IsLowerSnakeCase(field.name)
                           ? std::move(field.name)
                           : std::string("invalid_field_name");
    if (const auto* text = std::get_if<std::string>(&field.value)) {
      record.fields.push_back({name, active.RedactField(name, *text)});
    } else {
      record.fields.push_back({std::move(name), std::move(field.value)});
    }
  }
  sink->Write(record);
}

}

void SetLogLevel(Level level) {
  const std::uint32_t current =
      internal::LogState().load(std::memory_order_relaxed);
  internal::LogState().store(
      internal::PackLogState(level, current & kAllCategories),
      std::memory_order_relaxed);
}

void SetLogCategoryMask(std::uint32_t mask) {
  const std::uint32_t current =
      internal::LogState().load(std::memory_order_relaxed);
  internal::LogState().store(
      internal::PackLogState(static_cast<Level>(current >> 16), mask),
      std::memory_order_relaxed);
}

Level GetLogLevel() {
  return static_cast<Level>(
      internal::LogState().load(std::memory_order_relaxed) >> 16);
}

std::uint32_t GetLogCategoryMask() {
  return internal::LogState().load(std::memory_order_relaxed) & kAllCategories;
}

void SetSink(std::shared_ptr<Sink> sink) {
  Config& config = GetConfig();
  const base::AutoLock lock(config.lock);
  config.sink = std::move(sink);
}

void SetRedactor(std::shared_ptr<const base::UrlRedactor> redactor) {
  Config& config = GetConfig();
  const base::AutoLock lock(config.lock);
  config.redactor = std::move(redactor);
}

}
