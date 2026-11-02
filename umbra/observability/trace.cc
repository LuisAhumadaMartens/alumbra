#include "umbra/observability/trace.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "umbra/observability/category.h"

namespace umbra::observability {
namespace {

constexpr std::uint64_t kWriting = ~std::uint64_t{0};

struct Slot {
  std::atomic<std::uint64_t> ticket{0};
  std::atomic<std::uint64_t> timestamp_ns{0};
  std::atomic<std::uint64_t> packed{0};
  std::atomic<const char*> name{nullptr};
  std::atomic<std::int64_t> value{0};
};

struct Ring {
  std::atomic<std::uint64_t> head{0};
  std::array<Slot, kTraceRingCapacity> slots;
};

Ring& GetRing() {
  static Ring ring;
  return ring;
}

std::uint32_t CurrentThread() {
  static std::atomic<std::uint32_t> next_thread{0};
  thread_local const std::uint32_t id =
      next_thread.fetch_add(1, std::memory_order_relaxed) + 1;
  return id;
}

std::uint64_t NowNs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

std::uint64_t Pack(std::uint32_t thread, Category category, TracePhase phase) {
  return (static_cast<std::uint64_t>(thread) << 16) |
         (static_cast<std::uint64_t>(category) << 8) |
         static_cast<std::uint64_t>(phase);
}

}

void SetTraceCategoryMask(std::uint32_t mask) {
  internal::TraceMask().store(mask & kAllCategories, std::memory_order_relaxed);
}

std::uint32_t GetTraceCategoryMask() {
  return internal::TraceMask().load(std::memory_order_relaxed);
}

void RecordTrace(Category category, TracePhase phase, const char* name,
                 std::int64_t value) {
  Ring& ring = GetRing();
  const std::uint64_t ticket =
      ring.head.fetch_add(1, std::memory_order_relaxed);
  Slot& slot = ring.slots.at(ticket % kTraceRingCapacity);
  slot.ticket.store(kWriting, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  slot.timestamp_ns.store(NowNs(), std::memory_order_relaxed);
  slot.packed.store(Pack(CurrentThread(), category, phase),
                    std::memory_order_relaxed);
  slot.name.store(name, std::memory_order_relaxed);
  slot.value.store(value, std::memory_order_relaxed);
  slot.ticket.store(ticket + 1, std::memory_order_release);
}

std::vector<TraceEvent> DumpTrace() {
  Ring& ring = GetRing();
  const std::uint64_t head = ring.head.load(std::memory_order_acquire);
  const std::uint64_t first =
      head > kTraceRingCapacity ? head - kTraceRingCapacity : 0;
  std::vector<TraceEvent> events;
  events.reserve(static_cast<std::size_t>(head - first));
  for (std::uint64_t ticket = first; ticket < head; ++ticket) {
    const Slot& slot = ring.slots.at(ticket % kTraceRingCapacity);
    if (slot.ticket.load(std::memory_order_acquire) != ticket + 1) {
      continue;
    }
    const std::uint64_t timestamp =
        slot.timestamp_ns.load(std::memory_order_relaxed);
    const std::uint64_t packed = slot.packed.load(std::memory_order_relaxed);
    const char* name = slot.name.load(std::memory_order_relaxed);
    const std::int64_t value = slot.value.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (slot.ticket.load(std::memory_order_relaxed) != ticket + 1) {
      continue;
    }
    events.push_back({timestamp, static_cast<std::uint32_t>(packed >> 16),
                      static_cast<Category>((packed >> 8) & 0xFF),
                      static_cast<TracePhase>(packed & 0xFF), name, value});
  }
  return events;
}

std::string FormatTrace(std::span<const TraceEvent> events) {
  std::string text;
  for (const TraceEvent& event : events) {
    constexpr std::array<const char*, 4> kPhases = {"B", "E", "I", "C"};
    text += std::to_string(event.timestamp_ns);
    text += ' ';
    text += std::to_string(event.thread);
    text += ' ';
    text += kPhases.at(static_cast<std::size_t>(event.phase) % kPhases.size());
    text += ' ';
    text += CategoryName(event.category);
    text += ' ';
    text += event.name != nullptr ? event.name : "";
    if (event.phase == TracePhase::kCounter) {
      text += ' ';
      text += std::to_string(event.value);
    }
    text += '\n';
  }
  return text;
}

void ClearTrace() {
  Ring& ring = GetRing();
  for (Slot& slot : ring.slots) {
    slot.ticket.store(0, std::memory_order_relaxed);
  }
  ring.head.store(0, std::memory_order_release);
}

}
