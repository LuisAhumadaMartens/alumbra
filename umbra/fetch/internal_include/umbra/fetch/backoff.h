#ifndef UMBRA_FETCH_BACKOFF_H_
#define UMBRA_FETCH_BACKOFF_H_

#include <cstdint>
#include <random>

#include "absl/status/status.h"
#include "absl/time/time.h"

namespace umbra::fetch {

struct BackoffPolicy {
  int max_attempts = 3;
  absl::Duration initial_backoff = absl::Milliseconds(500);
  absl::Duration max_backoff = absl::Milliseconds(8000);
};

[[nodiscard]] absl::Status ValidateBackoffPolicy(const BackoffPolicy& policy);

class JitterSource {
 public:
  JitterSource() = default;
  JitterSource(const JitterSource&) = delete;
  JitterSource& operator=(const JitterSource&) = delete;
  virtual ~JitterSource() = default;

  [[nodiscard]] virtual double NextUnit() = 0;
};

class SeededJitter final : public JitterSource {
 public:
  explicit SeededJitter(uint64_t seed) : engine_(seed) {}

  [[nodiscard]] double NextUnit() override;

 private:
  std::mt19937_64 engine_;
};

[[nodiscard]] absl::Duration BackoffCeiling(const BackoffPolicy& policy,
                                            int attempt);

[[nodiscard]] absl::Duration BackoffDelay(const BackoffPolicy& policy,
                                          int attempt, JitterSource& jitter);

}

#endif
