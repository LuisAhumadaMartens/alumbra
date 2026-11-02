#ifndef UMBRA_OBSERVABILITY_TRACE_H_
#define UMBRA_OBSERVABILITY_TRACE_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "umbra/observability/category.h"

#ifndef ALUMBRA_TRACE_ENABLED
#define ALUMBRA_TRACE_ENABLED 1
#endif

namespace umbra::observability {

enum class TracePhase : std::uint8_t { kBegin, kEnd, kInstant, kCounter };

struct TraceEvent {
  std::uint64_t timestamp_ns;
  std::uint32_t thread;
  Category category;
  TracePhase phase;
  const char* name;
  std::int64_t value;
};

inline constexpr std::size_t kTraceRingCapacity = 8192;

namespace internal {

[[nodiscard]] inline std::atomic<std::uint32_t>& TraceMask() {
  static std::atomic<std::uint32_t> mask{0};
  return mask;
}

}

[[nodiscard]] inline bool TraceEnabled(Category category) {
  return (internal::TraceMask().load(std::memory_order_relaxed) &
          CategoryBit(category)) != 0;
}

void SetTraceCategoryMask(std::uint32_t mask);
[[nodiscard]] std::uint32_t GetTraceCategoryMask();

void RecordTrace(Category category, TracePhase phase, const char* name,
                 std::int64_t value);

[[nodiscard]] std::vector<TraceEvent> DumpTrace();
[[nodiscard]] std::string FormatTrace(std::span<const TraceEvent> events);
void ClearTrace();

class ScopedTrace {
 public:
  ScopedTrace(Category category, const char* name)
      : category_(category), name_(name), active_(TraceEnabled(category)) {
    if (active_) {
      RecordTrace(category_, TracePhase::kBegin, name_, 0);
    }
  }
  ScopedTrace(const ScopedTrace&) = delete;
  ScopedTrace& operator=(const ScopedTrace&) = delete;
  ~ScopedTrace() {
    if (active_) {
      RecordTrace(category_, TracePhase::kEnd, name_, 0);
    }
  }

 private:
  Category category_;
  const char* name_;
  bool active_;
};

}

#define ALUMBRA_INTERNAL_CONCAT_(a, b) a##b
#define ALUMBRA_INTERNAL_CONCAT(a, b) ALUMBRA_INTERNAL_CONCAT_(a, b)

#if ALUMBRA_TRACE_ENABLED

#define ALUMBRA_TRACE(category, name)                                \
  const ::umbra::observability::ScopedTrace ALUMBRA_INTERNAL_CONCAT( \
      alumbra_trace_scope_, __LINE__)(                               \
      ::umbra::observability::Category::category, "" name)

#define ALUMBRA_TRACE_INSTANT(category, name)            \
  if (!::umbra::observability::TraceEnabled(             \
          ::umbra::observability::Category::category)) { \
  } else                                                 \
    ::umbra::observability::RecordTrace(                 \
        ::umbra::observability::Category::category,      \
        ::umbra::observability::TracePhase::kInstant, "" name, 0)

#define ALUMBRA_TRACE_COUNTER(category, name, value)           \
  if (!::umbra::observability::TraceEnabled(                   \
          ::umbra::observability::Category::category)) {       \
  } else                                                       \
    ::umbra::observability::RecordTrace(                       \
        ::umbra::observability::Category::category,            \
        ::umbra::observability::TracePhase::kCounter, "" name, \
        static_cast<std::int64_t>(value))

#else

#define ALUMBRA_TRACE(category, name) static_assert(true)
#define ALUMBRA_TRACE_INSTANT(category, name) static_assert(true)
#define ALUMBRA_TRACE_COUNTER(category, name, value) \
  static_cast<void>(sizeof(value))

#endif

#endif
