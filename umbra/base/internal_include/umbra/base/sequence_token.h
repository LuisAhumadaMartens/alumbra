#ifndef UMBRA_BASE_SEQUENCE_TOKEN_H_
#define UMBRA_BASE_SEQUENCE_TOKEN_H_

#include <cstdint>
#include <source_location>

namespace umbra::base {

class SequenceToken {
 public:
  SequenceToken() = default;

  static SequenceToken Create();
  static SequenceToken GetForCurrentThread();

  bool IsValid() const { return value_ != 0; }

  friend bool operator==(SequenceToken lhs, SequenceToken rhs) = default;

 private:
  explicit SequenceToken(std::uint64_t value) : value_(value) {}

  std::uint64_t value_ = 0;
};

class ScopedTaskContext {
 public:
  ScopedTaskContext(SequenceToken token, std::source_location posted_from);
  ScopedTaskContext(const ScopedTaskContext&) = delete;
  ScopedTaskContext& operator=(const ScopedTaskContext&) = delete;
  ~ScopedTaskContext();

 private:
  SequenceToken previous_token_;
  std::source_location previous_posted_from_;
};

std::source_location GetCurrentTaskPostedFrom();

}

#endif
