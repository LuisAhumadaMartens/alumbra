#ifndef UMBRA_SOURCE_BYTE_STREAM_DATA_SOURCE_H_
#define UMBRA_SOURCE_BYTE_STREAM_DATA_SOURCE_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_provider.h"
#include "umbra/base/sequence_checker.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/url_redactor.h"
#include "umbra/source/data_source.h"
#include "umbra/source/read_request.h"
#include "umbra/source/sequenced_receiver.h"

namespace umbra::source {

inline constexpr size_t kDefaultPauseThresholdBytes = 262144;
inline constexpr size_t kDefaultSkipForwardBytes = 65536;

struct ByteStreamDataSourceOptions {
  size_t pause_threshold_bytes = kDefaultPauseThresholdBytes;
  size_t skip_forward_bytes = kDefaultSkipForwardBytes;
};

class OpenRequest {
 public:
  using Callback =
      absl::AnyInvocable<void(absl::StatusOr<std::shared_ptr<DataSource>>) &&>;

  static std::shared_ptr<OpenRequest> Create(Callback callback);

  explicit OpenRequest(Callback callback);
  OpenRequest(const OpenRequest&) = delete;
  OpenRequest& operator=(const OpenRequest&) = delete;
  ~OpenRequest() = default;

  void Retain(std::shared_ptr<DataSource> source);
  void Cancel();
  bool Complete(absl::StatusOr<std::shared_ptr<DataSource>> result);

  [[nodiscard]] bool Cancelled() const;
  [[nodiscard]] bool Completed() const;

 private:
  enum class State { kPending, kCancelled, kCompleted };

  Callback callback_;
  std::shared_ptr<DataSource> owner_;
  State state_ = State::kPending;
  base::SequenceChecker sequence_;
};

class ByteStreamDataSource final
    : public DataSource,
      public std::enable_shared_from_this<ByteStreamDataSource> {
 public:
  static std::shared_ptr<OpenRequest> Open(
      std::shared_ptr<penumbra::ByteStreamProvider> provider,
      penumbra::ByteStreamRequest request,
      std::shared_ptr<base::SequencedTaskRunner> runner,
      std::shared_ptr<const base::UrlRedactor> redactor,
      ByteStreamDataSourceOptions options, OpenRequest::Callback callback);

  ByteStreamDataSource(const ByteStreamDataSource&) = delete;
  ByteStreamDataSource& operator=(const ByteStreamDataSource&) = delete;
  ~ByteStreamDataSource() override;

  [[nodiscard]] const DataSourceInfo& Info() const override;
  std::shared_ptr<ReadRequest> Read(uint64_t offset, size_t size,
                                    ReadRequest::Callback callback) override;
  void Close() override;

  [[nodiscard]] size_t QueuedBytes() const;
  [[nodiscard]] bool Paused() const;

  void HandleOpen(penumbra::ByteStreamInfo info);
  void HandleData(penumbra::SharedBytes data);
  void HandleEnd();
  void HandleError(const absl::Status& error);

 private:
  struct Pending {
    uint64_t offset = 0;
    size_t size = 0;
    std::shared_ptr<ReadRequest> request;
  };

  ByteStreamDataSource(std::shared_ptr<penumbra::ByteStreamProvider> provider,
                       penumbra::ByteStreamRequest request,
                       std::shared_ptr<base::SequencedTaskRunner> runner,
                       std::shared_ptr<const base::UrlRedactor> redactor,
                       ByteStreamDataSourceOptions options);

  void StartStream(uint64_t offset);
  void StopStream();
  void FailOpen(absl::Status error);
  void Deliver(const std::shared_ptr<ReadRequest>& request,
               absl::StatusOr<penumbra::SharedBytes> result);
  void ServePending();
  void Append(penumbra::SharedBytes chunk);
  void DiscardTo(uint64_t offset);
  penumbra::SharedBytes Take(size_t size);
  void UpdateFlow();
  [[nodiscard]] absl::Status MapError(const absl::Status& error) const;
  [[nodiscard]] uint64_t Available() const { return tail_ - head_; }

  std::shared_ptr<penumbra::ByteStreamProvider> provider_;
  penumbra::ByteStreamRequest request_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  std::shared_ptr<const base::UrlRedactor> redactor_;
  ByteStreamDataSourceOptions options_;
  DataSourceInfo info_;
  std::shared_ptr<SequencedReceiver<ByteStreamDataSource>> receiver_;
  std::unique_ptr<penumbra::ByteStream> stream_;
  std::weak_ptr<OpenRequest> open_request_;
  bool open_pending_ = false;
  std::optional<Pending> pending_;
  std::deque<penumbra::SharedBytes> queue_;
  size_t front_skip_ = 0;
  size_t queued_ = 0;
  uint64_t head_ = 0;
  uint64_t tail_ = 0;
  uint64_t discard_until_ = 0;
  absl::Status stream_error_;
  bool opened_ = false;
  bool ended_ = false;
  bool dead_ = false;
  bool paused_ = false;
  bool closed_ = false;
  base::SequenceChecker sequence_;
};

}

#endif
