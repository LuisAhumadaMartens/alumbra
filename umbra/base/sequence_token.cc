#include "umbra/base/sequence_token.h"

#include <atomic>
#include <cstdint>
#include <source_location>

namespace umbra::base {
namespace {

struct ThreadState {
  SequenceToken current_token;
  SequenceToken thread_token;
  std::source_location posted_from;
};

ThreadState& GetThreadState() {
  static constinit thread_local ThreadState state;
  return state;
}

}

SequenceToken SequenceToken::Create() {
  static std::atomic<std::uint64_t> next_token{1};
  return SequenceToken(next_token.fetch_add(1, std::memory_order_relaxed));
}

SequenceToken SequenceToken::GetForCurrentThread() {
  auto& state = GetThreadState();
  if (state.current_token.IsValid()) {
    return state.current_token;
  }
  if (!state.thread_token.IsValid()) {
    state.thread_token = Create();
  }
  return state.thread_token;
}

ScopedTaskContext::ScopedTaskContext(SequenceToken token,
                                     std::source_location posted_from)
    : previous_token_(GetThreadState().current_token),
      previous_posted_from_(GetThreadState().posted_from) {
  GetThreadState().current_token = token;
  GetThreadState().posted_from = posted_from;
}

ScopedTaskContext::~ScopedTaskContext() {
  GetThreadState().current_token = previous_token_;
  GetThreadState().posted_from = previous_posted_from_;
}

std::source_location GetCurrentTaskPostedFrom() {
  return GetThreadState().posted_from;
}

}
