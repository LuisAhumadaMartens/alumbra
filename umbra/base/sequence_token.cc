#include "umbra/base/sequence_token.h"

#include <atomic>
#include <cstdint>
#include <source_location>

namespace umbra::base {
namespace {

std::atomic<std::uint64_t> g_next_token{1};
constinit thread_local SequenceToken t_current_token;
constinit thread_local SequenceToken t_thread_token;
constinit thread_local std::source_location t_posted_from;

}

SequenceToken SequenceToken::Create() {
  return SequenceToken(g_next_token.fetch_add(1, std::memory_order_relaxed));
}

SequenceToken SequenceToken::GetForCurrentThread() {
  if (t_current_token.IsValid()) {
    return t_current_token;
  }
  if (!t_thread_token.IsValid()) {
    t_thread_token = Create();
  }
  return t_thread_token;
}

ScopedTaskContext::ScopedTaskContext(SequenceToken token,
                                     std::source_location posted_from)
    : previous_token_(t_current_token), previous_posted_from_(t_posted_from) {
  t_current_token = token;
  t_posted_from = posted_from;
}

ScopedTaskContext::~ScopedTaskContext() {
  t_current_token = previous_token_;
  t_posted_from = previous_posted_from_;
}

std::source_location GetCurrentTaskPostedFrom() { return t_posted_from; }

}
