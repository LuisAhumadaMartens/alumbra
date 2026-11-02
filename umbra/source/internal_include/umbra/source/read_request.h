#ifndef UMBRA_SOURCE_READ_REQUEST_H_
#define UMBRA_SOURCE_READ_REQUEST_H_

#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/base/sequence_checker.h"

namespace umbra::source {

class ReadRequest {
 public:
  using Callback =
      absl::AnyInvocable<void(absl::StatusOr<penumbra::SharedBytes>) &&>;

  static std::shared_ptr<ReadRequest> Create(Callback callback);

  explicit ReadRequest(Callback callback);
  ReadRequest(const ReadRequest&) = delete;
  ReadRequest& operator=(const ReadRequest&) = delete;
  ~ReadRequest() = default;

  void Cancel();
  bool Complete(absl::StatusOr<penumbra::SharedBytes> result);

  [[nodiscard]] bool Cancelled() const;
  [[nodiscard]] bool Completed() const;

 private:
  enum class State { kPending, kCancelled, kCompleted };

  Callback callback_;
  State state_ = State::kPending;
  base::SequenceChecker sequence_;
};

}

#endif
