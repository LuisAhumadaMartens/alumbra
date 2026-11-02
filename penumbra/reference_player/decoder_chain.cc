#include "penumbra/reference_player/decoder_chain.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "penumbra/interface/callback_dispatcher.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra/interface/task_runner.h"
#include "penumbra/reference_player/decoder_selection.h"
#include "penumbra/reference_player/drop_attribution.h"

namespace penumbra {
namespace {

struct InstanceGate {
  explicit InstanceGate(SeekTicket initial) : ticket(initial) {}
  std::atomic<bool> retired{false};
  std::atomic<SeekTicket> ticket;
};

absl::Status Wrap(absl::StatusCode code, std::string_view message,
                  const absl::Status& cause) {
  absl::Status status(code, cause.ok()
                                ? std::string(message)
                                : absl::StrCat(message, ": ", cause.message()));
  cause.ForEachPayload([&](std::string_view url, const absl::Cord& payload) {
    status.SetPayload(url, payload);
  });
  return status;
}

bool IsCorrupted(const PortError& error) {
  return error.status.code() == absl::StatusCode::kDataLoss &&
         error.recovery == RecoveryHint::kTransient;
}

}

class DecoderChainCore : public std::enable_shared_from_this<DecoderChainCore> {
 public:
  explicit DecoderChainCore(DecoderChainConfig config);

  absl::Status Start(SeekTicket ticket);
  absl::Status WriteSamples(SeekTicket ticket,
                            std::span<const EncodedSample> samples);
  absl::Status EndOfStream(SeekTicket ticket);
  absl::Status Flush(SeekTicket ticket);
  absl::Status Stop();
  absl::Status SwitchToNextCandidate();
  void RecordFrameDue(TimeDelta now);
  void RecordDroppedFrame(DropCause cause, TimeDelta now);
  void PollDropTrigger(TimeDelta now);
  void UpdateDropThresholds(int32_t max_percent, TimeDelta window);
  DecoderChainStats GetStats() const;
  absl::Status GetDeliveryStatus() const;
  void OnEvent(uint64_t instance, DecoderEvent event);

 private:
  enum class FlushAction : uint8_t { kNone, kRecreateSame, kAdvance };

  absl::Status CheckSequence() const;
  absl::Status AdvanceCandidate(FallbackReason reason, absl::Status cause,
                                const StreamConfig& config);
  absl::Status RecreateCurrent(const StreamConfig& config);
  absl::StatusOr<std::unique_ptr<Decoder>> CreateInstance(
      const DecoderCandidate& candidate, const StreamConfig& config);
  void Install(std::unique_ptr<Decoder> decoder, size_t index,
               const StreamConfig& config);
  void RetireCurrent();
  void RetryRetired();
  void EmitFallback(DecoderFallbackEvent event);
  void Defer(absl::AnyInvocable<void(DecoderChainCore&) &&> task);
  void Deliver(DecoderEvent event);
  void DeliverError(absl::Status status, RecoveryHint hint);
  void Pump();
  void WritePending();
  void ReconfigureByRecreate();
  void HandleError(const PortError& error);
  void FailFatally(absl::Status status);
  absl::Status ExhaustedStatus(const absl::Status& cause) const;

  DecoderChainConfig config_;
  std::vector<DecoderCandidate> candidates_;
  size_t next_index_ = 0;
  size_t current_index_ = 0;
  std::string current_name_;
  std::unique_ptr<Decoder> decoder_;
  std::vector<std::unique_ptr<Decoder>> retiring_;
  std::shared_ptr<InstanceGate> gate_;
  uint64_t instance_counter_ = 0;
  uint64_t current_instance_ = 0;
  SeekTicket ticket_ = 0;
  uint32_t track_id_ = 0;
  StreamConfig stream_config_;
  StreamConfig retention_config_;
  std::vector<EncodedSample> pending_;
  std::vector<EncodedSample> retained_;
  bool can_refeed_ = true;
  bool has_output_ = false;
  bool failed_ = false;
  bool stopped_ = false;
  bool eos_requested_ = false;
  bool eos_sent_ = false;
  bool downstream_outstanding_ = false;
  bool any_decoder_started_ = false;
  size_t granted_max_ = 0;
  size_t decoder_grant_ = 0;
  FlushAction flush_action_ = FlushAction::kNone;
  FallbackReason flush_reason_ = FallbackReason::kErrorBeforeFirstOutput;
  absl::Status flush_cause_;
  absl::Status fatal_status_;
  absl::Status delivery_status_;
  DecoderChainStats stats_;
  DropAttribution drops_;
};

namespace {

class CandidateCallbacks final : public DecoderCallbacks {
 public:
  CandidateCallbacks(std::weak_ptr<DecoderChainCore> core,
                     std::shared_ptr<ControlTaskRunner> control,
                     std::shared_ptr<InstanceGate> gate, uint64_t instance)
      : core_(std::move(core)),
        control_(std::move(control)),
        gate_(std::move(gate)),
        instance_(instance) {}

  absl::Status OnDecoded(DecoderEvent event) override {
    if (gate_->retired.load() || event.ticket < gate_->ticket.load()) {
      return absl::OkStatus();
    }
    return control_->PostTask(
        [core = core_, instance = instance_,
         event = std::move(event)]() mutable {
          if (auto locked = core.lock()) {
            locked->OnEvent(instance, std::move(event));
          }
        },
        absl::ZeroDuration());
  }

 private:
  std::weak_ptr<DecoderChainCore> core_;
  std::shared_ptr<ControlTaskRunner> control_;
  std::shared_ptr<InstanceGate> gate_;
  uint64_t instance_;
};

}

DecoderChainCore::DecoderChainCore(DecoderChainConfig config)
    : config_(std::move(config)),
      stream_config_(config_.stream),
      retention_config_(config_.stream),
      drops_(config_.settings.dropped_frames_max_percent,
             TimeDelta::FromMilliseconds(
                 config_.settings.dropped_frames_window_ms)) {}

absl::Status DecoderChainCore::CheckSequence() const {
  assert(config_.control->RunsTasksInCurrentSequence());
  return stopped_ ? absl::FailedPreconditionError("decoder chain is stopped")
                  : absl::OkStatus();
}

absl::Status DecoderChainCore::Start(SeekTicket ticket) {
  PortCallScope const scope;
  if (config_.factory == nullptr || config_.capabilities == nullptr ||
      config_.control == nullptr || config_.downstream == nullptr ||
      ticket == 0 || config_.stream.info.type() == StreamType::kText) {
    return absl::InvalidArgumentError("invalid decoder chain configuration");
  }
  assert(config_.control->RunsTasksInCurrentSequence());
  ticket_ = ticket;
  track_id_ = config_.stream.info.track_id;
  auto available =
      config_.factory->GetDecoderCandidates(config_.stream.info.type());
  if (!available.ok()) {
    return available.status();
  }
  auto capability =
      config_.capabilities->QueryDecoding(config_.capability_request);
  if (!capability.ok()) {
    return capability.status();
  }
  if (!capability->supported) {
    return absl::UnimplementedError("no decoder supports the stream");
  }
  candidates_ = SelectDecoderCandidates(
      std::move(*available), {.stream = config_.stream,
                              .require_secure = config_.require_secure,
                              .settings = config_.settings,
                              .device = config_.device,
                              .blocklist = config_.blocklist});
  if (candidates_.empty()) {
    return absl::UnimplementedError(
        "no decoder remains after filtering for the stream");
  }
  return AdvanceCandidate(FallbackReason::kConfigurationFailed,
                          absl::OkStatus(), config_.stream);
}

absl::StatusOr<std::unique_ptr<Decoder>> DecoderChainCore::CreateInstance(
    const DecoderCandidate& candidate, const StreamConfig& config) {
  RetireCurrent();
  auto gate = std::make_shared<InstanceGate>(ticket_);
  const uint64_t instance = ++instance_counter_;
  auto callbacks = std::make_shared<CandidateCallbacks>(
      weak_from_this(), config_.control, gate, instance);
  auto created =
      config_.factory->CreateDecoder(candidate, config, ticket_, callbacks);
  if (!created.ok()) {
    return created.status();
  }
  if (*created == nullptr) {
    return absl::InternalError("decoder factory returned no decoder");
  }
  gate_ = std::move(gate);
  current_instance_ = instance;
  return created;
}

void DecoderChainCore::Install(std::unique_ptr<Decoder> decoder, size_t index,
                               const StreamConfig& config) {
  decoder_ = std::move(decoder);
  current_index_ = index;
  current_name_ = candidates_[index].name;
  stream_config_ = config;
  retention_config_ = config;
  has_output_ = false;
  failed_ = false;
  decoder_grant_ = 0;
  eos_sent_ = false;
  flush_action_ = FlushAction::kNone;
  any_decoder_started_ = true;
  pending_ = retained_;
}

void DecoderChainCore::RetireCurrent() {
  if (gate_ != nullptr) {
    gate_->retired.store(true);
  }
  if (decoder_ != nullptr) {
    if (!decoder_->Stop().ok()) {
      retiring_.push_back(std::move(decoder_));
    }
    decoder_.reset();
  }
}

void DecoderChainCore::RetryRetired() {
  std::erase_if(retiring_, [](const std::unique_ptr<Decoder>& decoder) {
    return decoder->Stop().ok();
  });
}

absl::Status DecoderChainCore::ExhaustedStatus(
    const absl::Status& cause) const {
  if (!any_decoder_started_ &&
      cause.code() == absl::StatusCode::kUnimplemented) {
    return Wrap(absl::StatusCode::kUnimplemented,
                "no decoder accepted the stream configuration", cause);
  }
  return Wrap(absl::StatusCode::kInternal, "every decoder failed", cause);
}

void DecoderChainCore::EmitFallback(DecoderFallbackEvent event) {
  if (!event.next.empty()) {
    ++stats_.fallbacks;
  }
  if (config_.observer == nullptr) {
    return;
  }
  auto observer = config_.observer;
  const auto status = config_.control->PostTask(
      [observer, event = std::move(event)]() { observer->OnFallback(event); },
      absl::ZeroDuration());
  if (!status.ok()) {
    delivery_status_ = status;
  }
}

absl::Status DecoderChainCore::AdvanceCandidate(FallbackReason reason,
                                                absl::Status cause,
                                                const StreamConfig& config) {
  std::string leaving = current_name_;
  absl::Status last = std::move(cause);
  const bool single_attempt = !config_.settings.fallback_enabled &&
                              reason == FallbackReason::kConfigurationFailed &&
                              current_name_.empty();
  while (true) {
    if (next_index_ >= candidates_.size()) {
      if (!leaving.empty()) {
        EmitFallback(
            {.previous = leaving, .next = "", .reason = reason, .cause = last});
      }
      return ExhaustedStatus(last);
    }
    const size_t index = next_index_++;
    if (!leaving.empty()) {
      EmitFallback({.previous = leaving,
                    .next = candidates_[index].name,
                    .reason = reason,
                    .cause = last});
    }
    auto created = CreateInstance(candidates_[index], config);
    if (created.ok()) {
      Install(std::move(*created), index, config);
      return absl::OkStatus();
    }
    last = created.status();
    leaving = candidates_[index].name;
    reason = FallbackReason::kConfigurationFailed;
    if (single_attempt) {
      return ExhaustedStatus(last);
    }
  }
}

absl::Status DecoderChainCore::RecreateCurrent(const StreamConfig& config) {
  ++stats_.recreations;
  auto created = CreateInstance(candidates_[current_index_], config);
  if (created.ok()) {
    Install(std::move(*created), current_index_, config);
    return absl::OkStatus();
  }
  if (!config_.settings.fallback_enabled) {
    return ExhaustedStatus(created.status());
  }
  return AdvanceCandidate(FallbackReason::kConfigurationFailed,
                          created.status(), config);
}

void DecoderChainCore::Defer(
    absl::AnyInvocable<void(DecoderChainCore&) &&> task) {
  const auto status = config_.control->PostTask(
      [weak = weak_from_this(), task = std::move(task)]() mutable {
        if (auto core = weak.lock()) {
          std::move(task)(*core);
        }
      },
      absl::ZeroDuration());
  if (!status.ok()) {
    delivery_status_ = status;
  }
}

void DecoderChainCore::Deliver(DecoderEvent event) {
  CallbackScope const scope;
  const auto status = config_.downstream->OnDecoded(std::move(event));
  if (!status.ok()) {
    delivery_status_ = status;
  }
}

void DecoderChainCore::DeliverError(absl::Status status, RecoveryHint hint) {
  Deliver({.ticket = ticket_,
           .detail = PortError{.status = std::move(status),
                               .category = PortErrorCategory::kDecode,
                               .recovery = hint}});
}

void DecoderChainCore::FailFatally(absl::Status status) {
  failed_ = true;
  fatal_status_ = status;
  pending_.clear();
  retained_.clear();
  DeliverError(std::move(status), RecoveryHint::kFatal);
}

void DecoderChainCore::HandleError(const PortError& error) {
  if (failed_ || stopped_) {
    return;
  }
  const bool corrupted = IsCorrupted(error);
  if (corrupted) {
    ++stats_.corrupted;
  }
  if (has_output_) {
    if (corrupted) {
      return;
    }
    failed_ = true;
    flush_action_ = FlushAction::kRecreateSame;
    pending_.clear();
    DeliverError(error.status, RecoveryHint::kRecreate);
    return;
  }
  if (!config_.settings.fallback_enabled) {
    FailFatally(Wrap(absl::StatusCode::kInternal,
                     "decoder failed before its first output", error.status));
    return;
  }
  if (!can_refeed_) {
    failed_ = true;
    flush_action_ = FlushAction::kAdvance;
    flush_reason_ = FallbackReason::kErrorBeforeFirstOutput;
    flush_cause_ = error.status;
    pending_.clear();
    DeliverError(error.status, RecoveryHint::kRecreate);
    return;
  }
  auto status = AdvanceCandidate(FallbackReason::kErrorBeforeFirstOutput,
                                 error.status, retention_config_);
  if (!status.ok()) {
    FailFatally(std::move(status));
    return;
  }
  Pump();
}

void DecoderChainCore::OnEvent(uint64_t instance, DecoderEvent event) {
  if (stopped_ || failed_ || instance != current_instance_ ||
      event.ticket != ticket_) {
    return;
  }
  if (const auto* needs = std::get_if<NeedsData>(&event.detail)) {
    if (needs->max_samples > 0) {
      decoder_grant_ = needs->max_samples;
      Pump();
    }
    return;
  }
  if (auto* error = std::get_if<PortError>(&event.detail)) {
    HandleError(*error);
    return;
  }
  if (std::holds_alternative<DecodedVideoFrame>(event.detail) ||
      std::holds_alternative<DecodedAudioBuffer>(event.detail)) {
    ++stats_.decoded;
    if (!has_output_) {
      has_output_ = true;
      retained_.clear();
    }
  }
  Deliver(std::move(event));
}

void DecoderChainCore::Pump() {
  if (stopped_ || failed_ || decoder_ == nullptr || decoder_grant_ == 0) {
    return;
  }
  if (!pending_.empty()) {
    WritePending();
    return;
  }
  if (eos_requested_) {
    if (!eos_sent_) {
      eos_sent_ = true;
      decoder_grant_ = 0;
      const auto status = decoder_->EndOfStream(ticket_);
      if (!status.ok()) {
        HandleError({.status = status,
                     .category = PortErrorCategory::kDecode,
                     .recovery = RecoveryHint::kRecreate});
      }
    }
    return;
  }
  if (!downstream_outstanding_) {
    downstream_outstanding_ = true;
    granted_max_ = decoder_grant_;
    Deliver({.ticket = ticket_,
             .detail = NeedsData{.track_id = track_id_,
                                 .max_samples = decoder_grant_}});
  }
}

void DecoderChainCore::WritePending() {
  size_t count = std::min(decoder_grant_, pending_.size());
  const auto change = std::find_if(
      pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(count),
      [](const EncodedSample& sample) {
        return sample.config_change != nullptr;
      });
  const auto change_index =
      static_cast<size_t>(std::distance(pending_.begin(), change));
  const bool leading_change = change_index == 0 && pending_[0].config_change;
  if (leading_change) {
    count = 1;
  } else if (change_index < count) {
    count = change_index;
  }
  decoder_grant_ = 0;
  const auto status = decoder_->WriteSamples(ticket_, {pending_.data(), count});
  if (status.ok()) {
    if (leading_change) {
      stream_config_.info = *pending_[0].config_change;
    }
    pending_.erase(pending_.begin(),
                   pending_.begin() + static_cast<std::ptrdiff_t>(count));
    return;
  }
  if (leading_change && has_output_ &&
      status.code() == absl::StatusCode::kUnimplemented) {
    ReconfigureByRecreate();
    return;
  }
  HandleError({.status = status,
               .category = PortErrorCategory::kDecode,
               .recovery = RecoveryHint::kRecreate});
}

void DecoderChainCore::ReconfigureByRecreate() {
  StreamConfig config = stream_config_;
  config.info = *pending_[0].config_change;
  retained_ = pending_;
  can_refeed_ = true;
  auto status = RecreateCurrent(config);
  if (!status.ok()) {
    FailFatally(std::move(status));
    return;
  }
  Pump();
}

absl::Status DecoderChainCore::WriteSamples(
    SeekTicket ticket, std::span<const EncodedSample> samples) {
  PortCallScope const scope;
  auto status = CheckSequence();
  if (!status.ok()) {
    return status;
  }
  if (ticket < ticket_) {
    return absl::OkStatus();
  }
  if (ticket != ticket_ || samples.empty()) {
    return absl::InvalidArgumentError("invalid decoder chain write");
  }
  if (!fatal_status_.ok()) {
    return fatal_status_;
  }
  if (!downstream_outstanding_ || eos_requested_) {
    return absl::FailedPreconditionError("decoder chain write without a grant");
  }
  if (samples.size() > granted_max_ ||
      std::ranges::any_of(samples, [this](const EncodedSample& sample) {
        return sample.track_id != track_id_;
      })) {
    return absl::InvalidArgumentError("invalid decoder chain samples");
  }
  downstream_outstanding_ = false;
  granted_max_ = 0;
  if (failed_) {
    return absl::OkStatus();
  }
  pending_.insert(pending_.end(), samples.begin(), samples.end());
  if (!has_output_ && can_refeed_) {
    retained_.insert(retained_.end(), samples.begin(), samples.end());
    if (retained_.size() > config_.max_retained_samples) {
      retained_.clear();
      can_refeed_ = false;
    }
  }
  RetryRetired();
  Defer([](DecoderChainCore& core) { core.Pump(); });
  return absl::OkStatus();
}

absl::Status DecoderChainCore::EndOfStream(SeekTicket ticket) {
  PortCallScope const scope;
  auto status = CheckSequence();
  if (!status.ok()) {
    return status;
  }
  if (ticket < ticket_) {
    return absl::OkStatus();
  }
  if (ticket != ticket_) {
    return absl::InvalidArgumentError("wrong decoder chain ticket");
  }
  if (!fatal_status_.ok()) {
    return fatal_status_;
  }
  if (!downstream_outstanding_ || eos_requested_) {
    return absl::FailedPreconditionError("decoder chain end without a grant");
  }
  downstream_outstanding_ = false;
  granted_max_ = 0;
  eos_requested_ = true;
  Defer([](DecoderChainCore& core) { core.Pump(); });
  return absl::OkStatus();
}

absl::Status DecoderChainCore::Flush(SeekTicket ticket) {
  PortCallScope const scope;
  auto status = CheckSequence();
  if (!status.ok()) {
    return status;
  }
  if (ticket <= ticket_) {
    return absl::InvalidArgumentError("decoder chain ticket must increase");
  }
  if (!fatal_status_.ok()) {
    return fatal_status_;
  }
  ticket_ = ticket;
  if (gate_ != nullptr) {
    gate_->ticket.store(ticket);
  }
  pending_.clear();
  retained_.clear();
  can_refeed_ = true;
  decoder_grant_ = 0;
  granted_max_ = 0;
  downstream_outstanding_ = false;
  eos_requested_ = false;
  eos_sent_ = false;
  drops_.RestartWindow();
  RetryRetired();
  auto action = flush_action_;
  if (action == FlushAction::kNone && !failed_) {
    status = decoder_->Flush(ticket);
    if (status.ok()) {
      return status;
    }
    action = FlushAction::kRecreateSame;
  }
  flush_action_ = FlushAction::kNone;
  if (action == FlushAction::kAdvance) {
    status = AdvanceCandidate(flush_reason_, flush_cause_, stream_config_);
  } else {
    status = RecreateCurrent(stream_config_);
  }
  if (!status.ok()) {
    failed_ = true;
    fatal_status_ = status;
  }
  return status;
}

absl::Status DecoderChainCore::Stop() {
  PortCallScope const scope;
  assert(config_.control->RunsTasksInCurrentSequence());
  if (stopped_) {
    return absl::OkStatus();
  }
  if (gate_ != nullptr) {
    gate_->retired.store(true);
  }
  RetryRetired();
  absl::Status status =
      decoder_ != nullptr ? decoder_->Stop() : absl::OkStatus();
  if (status.ok() && !retiring_.empty()) {
    status = absl::UnavailableError("a retired decoder is not yet quiescent");
  }
  if (!status.ok()) {
    return status;
  }
  decoder_.reset();
  pending_.clear();
  retained_.clear();
  stopped_ = true;
  return absl::OkStatus();
}

absl::Status DecoderChainCore::SwitchToNextCandidate() {
  auto status = CheckSequence();
  if (!status.ok()) {
    return status;
  }
  if (!config_.settings.fallback_enabled) {
    return absl::FailedPreconditionError("decoder fallback is disabled");
  }
  if (next_index_ >= candidates_.size()) {
    return absl::FailedPreconditionError("no decoder candidate remains");
  }
  flush_action_ = FlushAction::kAdvance;
  flush_reason_ = FallbackReason::kDroppedFrames;
  flush_cause_ = absl::OkStatus();
  return absl::OkStatus();
}

void DecoderChainCore::RecordFrameDue(TimeDelta now) {
  if (auto event = drops_.RecordFrameDue(now)) {
    ++stats_.drop_triggers;
    if (config_.observer != nullptr) {
      config_.observer->OnDropTrigger(*event);
    }
  }
}

void DecoderChainCore::RecordDroppedFrame(DropCause cause, TimeDelta now) {
  if (auto event = drops_.RecordDrop(cause, now)) {
    ++stats_.drop_triggers;
    if (config_.observer != nullptr) {
      config_.observer->OnDropTrigger(*event);
    }
  }
}

void DecoderChainCore::PollDropTrigger(TimeDelta now) {
  if (auto event = drops_.Poll(now)) {
    ++stats_.drop_triggers;
    if (config_.observer != nullptr) {
      config_.observer->OnDropTrigger(*event);
    }
  }
}

void DecoderChainCore::UpdateDropThresholds(int32_t max_percent,
                                            TimeDelta window) {
  drops_.Reconfigure(max_percent, window);
}

DecoderChainStats DecoderChainCore::GetStats() const {
  DecoderChainStats stats = stats_;
  stats.decoder_name = current_name_;
  if (!candidates_.empty() && !current_name_.empty()) {
    stats.kind = candidates_[current_index_].kind;
    stats.output = candidates_[current_index_].output;
  }
  stats.dropped_decode_late = drops_.DecodeLate();
  stats.dropped_present_late = drops_.PresentLate();
  stats.retained_samples = retained_.size();
  return stats;
}

absl::Status DecoderChainCore::GetDeliveryStatus() const {
  return delivery_status_;
}

absl::StatusOr<std::unique_ptr<DecoderChain>> DecoderChain::Create(
    DecoderChainConfig config, SeekTicket ticket) {
  auto core = std::make_shared<DecoderChainCore>(std::move(config));
  auto status = core->Start(ticket);
  if (!status.ok()) {
    return status;
  }
  return std::unique_ptr<DecoderChain>(new DecoderChain(std::move(core)));
}

DecoderChain::DecoderChain(std::shared_ptr<DecoderChainCore> core)
    : core_(std::move(core)) {}

DecoderChain::~DecoderChain() = default;

absl::Status DecoderChain::WriteSamples(
    SeekTicket ticket, std::span<const EncodedSample> samples) {
  return core_->WriteSamples(ticket, samples);
}

absl::Status DecoderChain::EndOfStream(SeekTicket ticket) {
  return core_->EndOfStream(ticket);
}

absl::Status DecoderChain::Flush(SeekTicket ticket) {
  return core_->Flush(ticket);
}

absl::Status DecoderChain::Stop() { return core_->Stop(); }

absl::Status DecoderChain::SwitchToNextCandidate() {
  return core_->SwitchToNextCandidate();
}

void DecoderChain::RecordFrameDue(TimeDelta now) { core_->RecordFrameDue(now); }

void DecoderChain::RecordDroppedFrame(DropCause cause, TimeDelta now) {
  core_->RecordDroppedFrame(cause, now);
}

void DecoderChain::PollDropTrigger(TimeDelta now) {
  core_->PollDropTrigger(now);
}

void DecoderChain::UpdateDropThresholds(int32_t max_percent, TimeDelta window) {
  core_->UpdateDropThresholds(max_percent, window);
}

DecoderChainStats DecoderChain::GetStats() const { return core_->GetStats(); }

absl::Status DecoderChain::GetDeliveryStatus() const {
  return core_->GetDeliveryStatus();
}

}
