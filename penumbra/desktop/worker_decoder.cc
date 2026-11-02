#include "penumbra/desktop/worker_decoder.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <utility>
#include <variant>

#include "absl/status/status.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {
namespace {

PortError ToPortError(const absl::Status& status) {
  if (status.code() == absl::StatusCode::kDataLoss) {
    return {.status = status,
            .category = PortErrorCategory::kDecode,
            .recovery = RecoveryHint::kTransient};
  }
  if (status.code() == absl::StatusCode::kUnimplemented) {
    return {.status = status,
            .category = PortErrorCategory::kCapability,
            .recovery = RecoveryHint::kFatal};
  }
  return {.status = status,
          .category = PortErrorCategory::kDecode,
          .recovery = RecoveryHint::kRecreate};
}

}

WorkerDecoder::WorkerDecoder(StreamInfo info, SeekTicket ticket,
                             std::shared_ptr<DecoderCallbacks> callbacks,
                             size_t grant_samples)
    : callbacks_(std::move(callbacks)),
      track_id_(info.track_id),
      grant_samples_(grant_samples),
      info_(std::move(info)),
      ticket_(ticket),
      active_ticket_(ticket) {}

WorkerDecoder::~WorkerDecoder() { (void)WorkerDecoder::Stop(); }

void WorkerDecoder::Start() {
  const std::scoped_lock lock(mutex_);
  thread_ = std::thread([this] { Run(); });
}

absl::Status WorkerDecoder::WriteSamples(
    SeekTicket ticket, std::span<const EncodedSample> samples) {
  const std::scoped_lock lock(mutex_);
  if (stopped_) {
    return absl::FailedPreconditionError("decoder is stopped");
  }
  if (ticket != ticket_) {
    return absl::InvalidArgumentError("wrong decoder ticket");
  }
  if (samples.empty() || samples.size() > outstanding_grant_) {
    return absl::InvalidArgumentError("write does not match the grant");
  }
  StreamInfo current = info_;
  for (const EncodedSample& sample : samples) {
    if (sample.config_change == nullptr) {
      continue;
    }
    auto status = CheckConfigChange(current, *sample.config_change);
    if (!status.ok()) {
      return status;
    }
    current = *sample.config_change;
  }
  info_ = std::move(current);
  outstanding_grant_ = 0;
  queue_.insert(queue_.end(), samples.begin(), samples.end());
  wake_.notify_all();
  return absl::OkStatus();
}

absl::Status WorkerDecoder::EndOfStream(SeekTicket ticket) {
  const std::scoped_lock lock(mutex_);
  if (stopped_) {
    return absl::FailedPreconditionError("decoder is stopped");
  }
  if (ticket != ticket_) {
    return absl::InvalidArgumentError("wrong decoder ticket");
  }
  if (outstanding_grant_ == 0 || eos_requested_) {
    return absl::FailedPreconditionError("end of stream without a grant");
  }
  outstanding_grant_ = 0;
  eos_requested_ = true;
  wake_.notify_all();
  return absl::OkStatus();
}

absl::Status WorkerDecoder::Flush(SeekTicket ticket) {
  std::unique_lock lock(mutex_);
  if (stopped_) {
    return absl::FailedPreconditionError("decoder is stopped");
  }
  if (ticket <= ticket_) {
    return absl::InvalidArgumentError("flush ticket must increase");
  }
  ticket_ = ticket;
  queue_.clear();
  eos_requested_ = false;
  eos_done_ = false;
  failed_ = false;
  outstanding_grant_ = 0;
  idle_.wait(lock, [this] { return !busy_; });
  Reset();
  grant_pending_ = true;
  wake_.notify_all();
  return absl::OkStatus();
}

absl::Status WorkerDecoder::Stop() {
  std::thread worker;
  {
    const std::scoped_lock lock(mutex_);
    stopped_ = true;
    queue_.clear();
    worker = std::move(thread_);
    wake_.notify_all();
    idle_.notify_all();
  }
  if (worker.joinable()) {
    worker.join();
  }
  return absl::OkStatus();
}

absl::Status WorkerDecoder::Emit(
    std::variant<DecodedVideoFrame, DecodedAudioBuffer> output) {
  if (auto* video = std::get_if<DecodedVideoFrame>(&output)) {
    return Deliver(active_ticket_, std::move(*video));
  }
  return Deliver(active_ticket_,
                 std::move(std::get<DecodedAudioBuffer>(output)));
}

absl::Status WorkerDecoder::Deliver(SeekTicket ticket, EventDetail detail) {
  return callbacks_->OnDecoded({.ticket = ticket, .detail = std::move(detail)});
}

void WorkerDecoder::Run() {
  std::unique_lock lock(mutex_);
  while (true) {
    wake_.wait(lock, [this] {
      if (stopped_) {
        return true;
      }
      if (failed_) {
        return false;
      }
      return !queue_.empty() || grant_pending_ ||
             (eos_requested_ && !eos_done_);
    });
    if (stopped_) {
      return;
    }
    active_ticket_ = ticket_;
    busy_ = true;
    absl::Status failure;
    bool corrupt_only = false;
    if (!queue_.empty()) {
      EncodedSample sample = std::move(queue_.front());
      queue_.pop_front();
      lock.unlock();
      failure = Decode(sample);
      corrupt_only = failure.code() == absl::StatusCode::kDataLoss;
      lock.lock();
    } else if (eos_requested_ && !eos_done_) {
      lock.unlock();
      failure = Drain();
      if (failure.code() == absl::StatusCode::kDataLoss) {
        (void)Deliver(active_ticket_, ToPortError(failure));
        failure = absl::OkStatus();
      }
      if (failure.ok()) {
        failure = Deliver(active_ticket_,
                          StateChanged{.state = PlayerState::kEndOfStream,
                                       .track_id = track_id_});
      }
      lock.lock();
      eos_done_ = true;
    } else {
      grant_pending_ = false;
      outstanding_grant_ = grant_samples_;
      lock.unlock();
      failure = Deliver(
          active_ticket_,
          NeedsData{.track_id = track_id_, .max_samples = grant_samples_});
      lock.lock();
    }
    if (!failure.ok() && !stopped_ && active_ticket_ == ticket_) {
      const PortError error = ToPortError(failure);
      lock.unlock();
      (void)Deliver(active_ticket_, error);
      lock.lock();
      if (!corrupt_only) {
        failed_ = true;
        queue_.clear();
      }
    }
    if (!failed_ && queue_.empty() && !eos_requested_ &&
        outstanding_grant_ == 0 && active_ticket_ == ticket_) {
      grant_pending_ = true;
    }
    busy_ = false;
    idle_.notify_all();
  }
}

}
