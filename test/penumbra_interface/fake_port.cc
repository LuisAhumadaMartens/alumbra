#include "fake_port.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "penumbra/interface/callback_dispatcher.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/encryption_info.h"
#include "penumbra/interface/interface.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra/interface/task_runner.h"
#include "umbra/base/task_runner.h"

namespace penumbra::test {
namespace {

absl::Status CheckBounds(OutputBounds bounds) {
  if (bounds.width < 0 || bounds.height < 0) {
    return absl::InvalidArgumentError("negative output extent");
  }
  return absl::OkStatus();
}

absl::Status CheckConfig(const StreamConfig& config) {
  if (config.info.codec == CodecId::kUnknown ||
      config.info.type() == StreamType::kText) {
    return absl::UnimplementedError("fake codec is unsupported");
  }
  if (config.info.type() == StreamType::kAudio) {
    const auto& format = std::get<AudioInfo>(config.info.format);
    if (format.sample_rate <= 0 || format.channel_count != 2 ||
        format.sample_format != SampleFormat::kF32) {
      return absl::UnimplementedError(
          "fake decoder emits stereo float audio only");
    }
  }
  return absl::OkStatus();
}

absl::Status CheckSample(const EncodedSample& sample,
                         const StreamConfig& config) {
  if (sample.track_id != config.info.track_id ||
      ValidateEncryption(sample) != EncryptionCheck::kValid ||
      (sample.encryption.has_value() &&
       config.encryption_scheme != sample.encryption->scheme)) {
    return absl::InvalidArgumentError("invalid sample track or encryption");
  }
  if (sample.config_change != nullptr) {
    if (sample.config_change->track_id != sample.track_id) {
      return absl::InvalidArgumentError("configuration changes track identity");
    }
    if (sample.config_change->codec != config.info.codec ||
        sample.config_change->type() != config.info.type()) {
      return absl::UnimplementedError(
          "configuration change requires recreation");
    }
    return CheckConfig({.info = *sample.config_change,
                        .encryption_scheme = config.encryption_scheme,
                        .output_mode = config.output_mode});
  }
  return absl::OkStatus();
}

}

RunnerAdapter::RunnerAdapter(
    std::shared_ptr<umbra::base::SequencedTaskRunner> runner)
    : runner_(std::move(runner)) {}

absl::Status RunnerAdapter::PostTask(Task task, absl::Duration delay) {
  if (!task || delay < absl::ZeroDuration() ||
      delay == absl::InfiniteDuration()) {
    return absl::InvalidArgumentError("invalid task or delay");
  }
  if (!runner_->PostDelayedTask(std::move(task), delay)) {
    return absl::UnavailableError("task runner is shut down");
  }
  return absl::OkStatus();
}

bool RunnerAdapter::RunsTasksInCurrentSequence() const {
  return runner_->RunsTasksInCurrentSequence();
}

FakePlayer::FakePlayer(PlayerConfig config,
                       std::shared_ptr<PlayerCallbacks> callbacks,
                       std::shared_ptr<InstanceBudget> budget)
    : config_(std::move(config)),
      ticket_(config_.initial_ticket),
      callbacks_(std::move(callbacks)),
      budget_(std::move(budget)) {
  ++budget_->active;
}

FakePlayer::~FakePlayer() { --budget_->active; }

absl::Status FakePlayer::CheckRunning() const {
  PortCallScope const scope;
  assert(sequence_.CalledOnValidSequence());
  if (stopped_) {
    return absl::FailedPreconditionError("player is stopped");
  }
  return absl::OkStatus();
}

const StreamConfig* FakePlayer::FindStream(uint32_t track_id) const {
  auto it = std::ranges::find_if(config_.streams,
                                 [track_id](const StreamConfig& stream) {
                                   return stream.info.track_id == track_id;
                                 });
  return it == config_.streams.end() ? nullptr : &*it;
}

absl::Status FakePlayer::ConsumeGrant(uint32_t track_id, size_t count) {
  if (FindStream(track_id) == nullptr || count == 0) {
    return absl::InvalidArgumentError("unknown track or empty write");
  }
  auto it = grants_.find(track_id);
  if (it == grants_.end() || ended_[track_id]) {
    return absl::FailedPreconditionError("write without needs-data grant");
  }
  if (count > it->second) {
    return absl::InvalidArgumentError("write exceeds needs-data grant");
  }
  grants_.erase(it);
  return absl::OkStatus();
}

absl::Status FakePlayer::WriteSamples(SeekTicket ticket, uint32_t track_id,
                                      std::span<const EncodedSample> samples) {
  auto status = CheckRunning();
  if (!status.ok()) {
    return status;
  }
  if (ticket < ticket_) {
    return absl::OkStatus();
  }
  if (ticket != ticket_ || config_.input_mode != InputMode::kSamples) {
    return absl::InvalidArgumentError("wrong ticket or input mode");
  }
  const auto* stream = FindStream(track_id);
  if (stream == nullptr) {
    return absl::InvalidArgumentError("unknown track");
  }
  for (const auto& sample : samples) {
    status = CheckSample(sample, *stream);
    if (!status.ok()) {
      return status;
    }
  }
  status = ConsumeGrant(track_id, samples.size());
  if (!status.ok()) {
    return status;
  }
  for (const auto& sample : samples) {
    if (sample.config_change != nullptr) {
      for (auto& configured : config_.streams) {
        if (configured.info.track_id == track_id) {
          configured.info = *sample.config_change;
        }
      }
    }
  }
  samples_.insert(samples_.end(), samples.begin(), samples.end());
  snapshot_.frames.decoded += samples.size();
  return absl::OkStatus();
}

absl::Status FakePlayer::WriteFragment(SeekTicket ticket,
                                       uint32_t requested_track,
                                       const MediaFragment& fragment) {
  auto status = CheckRunning();
  if (!status.ok()) {
    return status;
  }
  if (ticket < ticket_) {
    return absl::OkStatus();
  }
  if (ticket != ticket_ || config_.input_mode != InputMode::kFragments ||
      fragment.data.empty() || fragment.track_ids.empty() ||
      std::ranges::find(fragment.track_ids, requested_track) ==
          fragment.track_ids.end()) {
    return absl::InvalidArgumentError("invalid fragment or ticket");
  }
  for (auto track : fragment.track_ids) {
    if (FindStream(track) == nullptr || ended_[track]) {
      return absl::InvalidArgumentError(
          "fragment contains unknown or ended track");
    }
  }
  status = ConsumeGrant(requested_track, 1);
  if (!status.ok()) {
    return status;
  }
  fragments_.push_back(fragment);
  return absl::OkStatus();
}

absl::Status FakePlayer::EndOfStream(SeekTicket ticket, uint32_t track_id) {
  auto status = CheckRunning();
  if (!status.ok()) {
    return status;
  }
  if (ticket < ticket_) {
    return absl::OkStatus();
  }
  if (ticket != ticket_ || FindStream(track_id) == nullptr) {
    return absl::InvalidArgumentError("unknown track or ticket");
  }
  status = ConsumeGrant(track_id, 1);
  if (!status.ok()) {
    return status;
  }
  ended_[track_id] = true;
  CallbackScope const scope;
  status = callbacks_->OnEvent(
      {.ticket = ticket_,
       .detail = StateChanged{.state = PlayerState::kEndOfStream,
                              .track_id = track_id}});
  if (!status.ok()) {
    return status;
  }
  const bool all_drained =
      std::ranges::all_of(config_.streams, [this](const StreamConfig& stream) {
        return ended_[stream.info.track_id];
      });
  if (all_drained && !aggregate_ended_) {
    aggregate_ended_ = true;
    return callbacks_->OnEvent(
        {.ticket = ticket_,
         .detail = StateChanged{.state = PlayerState::kEndOfStream,
                                .track_id = std::nullopt}});
  }
  return absl::OkStatus();
}

absl::Status FakePlayer::AdvanceTicket(SeekTicket ticket) {
  auto status = CheckRunning();
  if (!status.ok()) {
    return status;
  }
  if (ticket <= ticket_) {
    return absl::InvalidArgumentError("seek ticket must increase");
  }
  ticket_ = ticket;
  grants_.clear();
  ended_.clear();
  aggregate_ended_ = false;
  samples_.clear();
  fragments_.clear();
  snapshot_.frames = {};
  return absl::OkStatus();
}

absl::Status FakePlayer::Seek(SeekTicket ticket, MediaTime position) {
  auto status = CheckRunning();
  if (!status.ok()) {
    return status;
  }
  if (!position.is_finite()) {
    return absl::InvalidArgumentError("seek position must be finite");
  }
  status = AdvanceTicket(ticket);
  if (status.ok()) {
    snapshot_.media_time = position;
  }
  return status;
}

absl::Status FakePlayer::Flush(SeekTicket ticket) {
  return AdvanceTicket(ticket);
}

absl::Status FakePlayer::SetPlaybackRate(double rate) {
  auto status = CheckRunning();
  if (!status.ok()) {
    return status;
  }
  if (!std::isfinite(rate) || rate < 0) {
    return absl::InvalidArgumentError("invalid playback rate");
  }
  return absl::OkStatus();
}

absl::Status FakePlayer::SetVolume(double volume) {
  auto status = CheckRunning();
  if (!status.ok()) {
    return status;
  }
  if (!std::isfinite(volume) || volume < 0 || volume > 1) {
    return absl::InvalidArgumentError("volume must be in [0, 1]");
  }
  return absl::OkStatus();
}

absl::Status FakePlayer::SetBounds(uint32_t track_id, OutputBounds bounds) {
  auto status = CheckRunning();
  if (!status.ok()) {
    return status;
  }
  const auto* stream = FindStream(track_id);
  if (stream == nullptr || stream->info.type() != StreamType::kVideo ||
      stream->output_mode != OutputMode::kPunchOut) {
    return absl::InvalidArgumentError("bounds require a punch-out video track");
  }
  status = CheckBounds(bounds);
  if (status.ok()) {
    bounds_[track_id] = bounds;
  }
  return status;
}

absl::StatusOr<PlaybackSnapshot> FakePlayer::GetSnapshot() const {
  auto status = CheckRunning();
  if (!status.ok()) {
    return status;
  }
  return snapshot_;
}

absl::StatusOr<std::optional<DecodedVideoFrame>> FakePlayer::GetCurrentFrame(
    uint32_t track_id) const {
  auto status = CheckRunning();
  if (!status.ok()) {
    return status;
  }
  const auto* stream = FindStream(track_id);
  if (stream == nullptr || stream->info.type() != StreamType::kVideo ||
      stream->output_mode != OutputMode::kTexture) {
    return absl::InvalidArgumentError("frame requires a texture video track");
  }
  return std::optional<DecodedVideoFrame>{};
}

absl::Status FakePlayer::Stop() {
  PortCallScope const scope;
  assert(sequence_.CalledOnValidSequence());
  stopped_ = true;
  grants_.clear();
  samples_.clear();
  fragments_.clear();
  callbacks_.reset();
  return absl::OkStatus();
}

absl::Status FakePlayer::RequestData(uint32_t track_id, size_t max_samples) {
  auto status = CheckRunning();
  if (!status.ok()) {
    return status;
  }
  if (FindStream(track_id) == nullptr || max_samples == 0) {
    return absl::InvalidArgumentError("invalid needs-data request");
  }
  if (grants_.contains(track_id) || ended_[track_id]) {
    return absl::FailedPreconditionError(
        "track already has a grant or is ended");
  }
  grants_[track_id] = max_samples;
  CallbackScope const scope;
  status = callbacks_->OnEvent(
      {.ticket = ticket_,
       .detail = NeedsData{.track_id = track_id, .max_samples = max_samples}});
  if (!status.ok()) {
    grants_.erase(track_id);
  }
  return status;
}

absl::Status FakePlayer::ReportError(PortError error) {
  auto status = CheckRunning();
  if (!status.ok()) {
    return status;
  }
  if (error.status.ok()) {
    return absl::InvalidArgumentError("error callback needs a non-OK status");
  }
  CallbackScope const scope;
  return callbacks_->OnEvent({.ticket = ticket_, .detail = std::move(error)});
}

FakeDecoder::FakeDecoder(StreamConfig config, SeekTicket ticket,
                         std::shared_ptr<DecoderCallbacks> callbacks)
    : config_(std::move(config)),
      ticket_(ticket),
      callbacks_(std::move(callbacks)) {}

absl::Status FakeDecoder::CheckRunning() const {
  PortCallScope const scope;
  assert(sequence_.CalledOnValidSequence());
  return stopped_ ? absl::FailedPreconditionError("decoder is stopped")
                  : absl::OkStatus();
}

absl::Status FakeDecoder::WriteSamples(SeekTicket ticket,
                                       std::span<const EncodedSample> samples) {
  auto status = CheckRunning();
  if (!status.ok()) {
    return status;
  }
  if (ticket < ticket_) {
    return absl::OkStatus();
  }
  if (ticket != ticket_ || samples.empty() || samples.size() > grant_) {
    return absl::InvalidArgumentError("invalid decoder write");
  }
  if (ended_) {
    return absl::FailedPreconditionError("decoder is ended");
  }
  for (const auto& sample : samples) {
    status = CheckSample(sample, config_);
    if (!status.ok()) {
      return status;
    }
    if (!ToMediaTime(sample.pts, Rounding::kNearest).has_value()) {
      return absl::InvalidArgumentError("invalid decoder timestamp");
    }
  }
  grant_ = 0;
  for (const auto& sample : samples) {
    const auto presentation_time = ToMediaTime(sample.pts, Rounding::kNearest);
    if (!presentation_time.has_value()) {
      return absl::InvalidArgumentError("invalid decoder timestamp");
    }
    if (sample.config_change != nullptr) {
      config_.info = *sample.config_change;
    }
    CallbackScope const scope;
    if (config_.info.type() == StreamType::kVideo) {
      DecodedVideoFrame frame;
      auto storage = std::make_shared<std::array<uint8_t, 6>>();
      frame.storage = storage;
      frame.presentation_time = *presentation_time;
      frame.width = frame.height = 2;
      frame.format = PixelFormat::kI420;
      frame.planes = {{{.data = storage->data(), .stride = 2},
                       {.data = &(*storage)[4], .stride = 1},
                       {.data = &(*storage)[5], .stride = 1}}};
      status = callbacks_->OnDecoded(
          {.ticket = ticket_, .detail = std::move(frame)});
    } else {
      DecodedAudioBuffer buffer;
      auto storage = std::make_shared<std::array<uint8_t, 8>>();
      buffer.storage = storage;
      buffer.presentation_time = *presentation_time;
      const auto& audio = std::get<AudioInfo>(config_.info.format);
      buffer.sample_rate = audio.sample_rate;
      buffer.channel_count = audio.channel_count;
      buffer.format = audio.sample_format;
      buffer.frame_count = 1;
      buffer.planes[0] = storage->data();
      status = callbacks_->OnDecoded(
          {.ticket = ticket_, .detail = std::move(buffer)});
    }
    if (!status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

absl::Status FakeDecoder::EndOfStream(SeekTicket ticket) {
  auto status = CheckRunning();
  if (!status.ok()) {
    return status;
  }
  if (ticket < ticket_) {
    return absl::OkStatus();
  }
  if (ticket != ticket_) {
    return absl::InvalidArgumentError("wrong decoder ticket");
  }
  if (grant_ == 0 || ended_) {
    return absl::FailedPreconditionError("decoder end without a grant");
  }
  grant_ = 0;
  ended_ = true;
  CallbackScope const scope;
  return callbacks_->OnDecoded(
      {.ticket = ticket_,
       .detail = StateChanged{.state = PlayerState::kEndOfStream,
                              .track_id = config_.info.track_id}});
}

absl::Status FakeDecoder::Flush(SeekTicket ticket) {
  auto status = CheckRunning();
  if (!status.ok()) {
    return status;
  }
  if (ticket <= ticket_) {
    return absl::InvalidArgumentError("decoder ticket must increase");
  }
  ticket_ = ticket;
  grant_ = 0;
  ended_ = false;
  return absl::OkStatus();
}

absl::Status FakeDecoder::Stop() {
  PortCallScope const scope;
  assert(sequence_.CalledOnValidSequence());
  stopped_ = true;
  grant_ = 0;
  callbacks_.reset();
  return absl::OkStatus();
}

absl::Status FakeDecoder::RequestData(size_t count) {
  auto status = CheckRunning();
  if (!status.ok()) {
    return status;
  }
  if (count == 0) {
    return absl::InvalidArgumentError("empty decoder grant");
  }
  if (grant_ != 0 || ended_) {
    return absl::FailedPreconditionError("decoder has a grant or is ended");
  }
  grant_ = count;
  CallbackScope const scope;
  status = callbacks_->OnDecoded(
      {.ticket = ticket_,
       .detail =
           NeedsData{.track_id = config_.info.track_id, .max_samples = count}});
  if (!status.ok()) {
    grant_ = 0;
  }
  return status;
}

void SourceCallBarrier::Open() {
  assert(state_.load() == kClosed);
  state_.store(0, std::memory_order_release);
}

bool SourceCallBarrier::TryEnter() {
  auto state = state_.load(std::memory_order_acquire);
  while ((state & kClosed) == 0) {
    if (state == kClosed - 1) {
      return false;
    }
    if (state_.compare_exchange_weak(state, state + 1,
                                     std::memory_order_acquire,
                                     std::memory_order_relaxed)) {
      return true;
    }
  }
  return false;
}

void SourceCallBarrier::Leave() {
  state_.fetch_sub(1, std::memory_order_release);
}

bool SourceCallBarrier::Close() {
  return (state_.fetch_or(kClosed, std::memory_order_acq_rel) &
          (kClosed - 1)) == 0;
}

absl::Status FakeVideoSink::Start(std::shared_ptr<VideoFrameSource> frames) {
  PortCallScope const scope;
  if (stopped_ || frames_ != nullptr) {
    return absl::FailedPreconditionError(
        "video sink stopped or already started");
  }
  if (frames == nullptr) {
    return absl::InvalidArgumentError("missing frame source");
  }
  frames_ = std::move(frames);
  source_calls_.Open();
  return absl::OkStatus();
}

absl::Status FakeVideoSink::SetBounds(OutputBounds bounds) {
  PortCallScope const scope;
  return stopped_ ? absl::FailedPreconditionError("video sink stopped")
                  : CheckBounds(bounds);
}

absl::Status FakeVideoSink::Stop() {
  PortCallScope const scope;
  stopped_ = true;
  if (!source_calls_.Close()) {
    return absl::UnavailableError("video source call is in flight");
  }
  frames_.reset();
  return absl::OkStatus();
}

std::optional<DecodedVideoFrame> FakeVideoSink::Vsync(MediaTime time) {
  if (!source_calls_.TryEnter()) {
    return std::nullopt;
  }
  CallbackScope const scope;
  auto frame = frames_->TakeDueFrame(time);
  source_calls_.Leave();
  return frame;
}

absl::Status FakeAudioSink::Start(AudioInfo format,
                                  std::shared_ptr<AudioSource> audio) {
  PortCallScope const scope;
  if (stopped_ || audio_ != nullptr) {
    return absl::FailedPreconditionError(
        "audio sink stopped or already started");
  }
  if (audio == nullptr || format.sample_rate <= 0 ||
      format.channel_count <= 0 ||
      format.sample_format == SampleFormat::kUnknown) {
    return absl::InvalidArgumentError("invalid audio source or format");
  }
  if (format.sample_format != SampleFormat::kF32) {
    return absl::UnimplementedError("fake audio sink supports interleaved F32");
  }
  format_ = format;
  audio_ = std::move(audio);
  source_calls_.Open();
  return absl::OkStatus();
}

absl::StatusOr<TimeDelta> FakeAudioSink::GetOutputLatency() const {
  PortCallScope const scope;
  if (stopped_) {
    return absl::FailedPreconditionError("audio sink stopped");
  }
  if (invalid_read_.load()) {
    return absl::DataLossError("audio source returned invalid frame bytes");
  }
  return TimeDelta::Zero();
}

absl::Status FakeAudioSink::Stop() {
  PortCallScope const scope;
  stopped_ = true;
  if (!source_calls_.Close()) {
    return absl::UnavailableError("audio source call is in flight");
  }
  audio_.reset();
  return absl::OkStatus();
}

size_t FakeAudioSink::Render(std::span<uint8_t> destination) {
  if (!source_calls_.TryEnter()) {
    std::ranges::fill(destination, 0);
    return 0;
  }
  CallbackScope const scope;
  const size_t frame_bytes =
      static_cast<size_t>(format_.channel_count) * sizeof(float);
  size_t read = 0;
  if (destination.size() % frame_bytes == 0) {
    read = audio_->Read(destination);
  } else {
    invalid_read_.store(true);
  }
  if (read > destination.size() || read % frame_bytes != 0) {
    invalid_read_.store(true);
    read = 0;
  }
  std::fill(destination.begin() + static_cast<std::ptrdiff_t>(read),
            destination.end(), 0);
  source_calls_.Leave();
  return read;
}

FakePort::FakePort(std::shared_ptr<ControlTaskRunner> runner)
    : runner_(std::move(runner)) {}

absl::StatusOr<PortLimits> FakePort::GetLimits() const {
  PortCallScope const scope;
  return PortLimits{};
}

absl::StatusOr<std::unique_ptr<Player>> FakePort::CreatePlayer(
    PlayerConfig config, std::shared_ptr<PlayerCallbacks> callbacks) {
  PortCallScope const scope;
  if (config.api_version != kApiVersion || config.initial_ticket == 0 ||
      config.streams.empty() || callbacks == nullptr) {
    return absl::InvalidArgumentError("invalid player configuration");
  }
  std::vector<uint32_t> tracks;
  for (const auto& stream : config.streams) {
    auto status = CheckConfig(stream);
    if (!status.ok()) {
      return status;
    }
    if (std::ranges::find(tracks, stream.info.track_id) != tracks.end()) {
      return absl::InvalidArgumentError("duplicate track");
    }
    tracks.push_back(stream.info.track_id);
  }
  if (budget_->active >= 1) {
    return absl::ResourceExhaustedError("fake player instance limit reached");
  }
  return std::make_unique<FakePlayer>(std::move(config), std::move(callbacks),
                                      budget_);
}

absl::StatusOr<DecodingCapability> FakePort::QueryDecoding(
    const CapabilityRequest& request) const {
  PortCallScope const scope;
  DecodingCapability result;
  result.supported =
      CheckConfig({.info = request.stream,
                   .encryption_scheme = request.encryption_scheme,
                   .output_mode = OutputMode::kTexture})
          .ok() &&
      request.key_system.empty();
  result.smooth = result.supported;
  result.power_efficient = false;
  result.accepts_fragments = result.supported;
  return result;
}

absl::StatusOr<bool> FakePort::CanChangeType(const StreamConfig& from,
                                             const StreamConfig& to) const {
  PortCallScope const scope;
  return from.info.type() == to.info.type() &&
         from.info.codec == to.info.codec &&
         from.encryption_scheme == to.encryption_scheme &&
         from.output_mode == to.output_mode && CheckConfig(to).ok();
}

absl::StatusOr<std::vector<DecoderCandidate>> FakePort::GetDecoderCandidates(
    StreamType type) const {
  PortCallScope const scope;
  if (type == StreamType::kText) {
    return absl::UnimplementedError("fake port has no text decoders");
  }
  DecoderCandidate candidate;
  candidate.name = type == StreamType::kVideo ? "fake.video" : "fake.audio";
  candidate.kind = DecoderKind::kBundledSoftware;
  candidate.codecs = {CodecId::kH264, CodecId::kHevc, CodecId::kVp9,
                      CodecId::kAv1,  CodecId::kAac,  CodecId::kOpus,
                      CodecId::kAc3,  CodecId::kEac3, CodecId::kFlac,
                      CodecId::kMp3};
  return std::vector<DecoderCandidate>{std::move(candidate)};
}

absl::StatusOr<std::unique_ptr<Decoder>> FakePort::CreateDecoder(
    const DecoderCandidate& candidate, StreamConfig config, SeekTicket ticket,
    std::shared_ptr<DecoderCallbacks> callbacks) {
  PortCallScope const scope;
  if (candidate.name.empty() || config.info.type() == StreamType::kText ||
      ticket == 0 || callbacks == nullptr) {
    return absl::InvalidArgumentError("invalid decoder configuration");
  }
  auto status = CheckConfig(config);
  if (!status.ok()) {
    return status;
  }
  return std::make_unique<FakeDecoder>(std::move(config), ticket,
                                       std::move(callbacks));
}

absl::StatusOr<std::unique_ptr<VideoSink>> FakePort::CreateVideoSink(
    OutputMode mode) {
  PortCallScope const scope;
  if (mode != OutputMode::kTexture && mode != OutputMode::kPunchOut) {
    return absl::InvalidArgumentError("unknown output mode");
  }
  return std::make_unique<FakeVideoSink>();
}

absl::StatusOr<std::unique_ptr<AudioSink>> FakePort::CreateAudioSink() {
  PortCallScope const scope;
  return std::make_unique<FakeAudioSink>();
}

absl::StatusOr<std::shared_ptr<ControlTaskRunner>> FakePort::CreateTaskRunner(
    TaskPriority priority, BlockingPolicy blocking) {
  PortCallScope const scope;
  if (blocking == BlockingPolicy::kMayBlock) {
    return absl::UnimplementedError("manual event-loop runner cannot block");
  }
  if (priority != TaskPriority::kControl &&
      priority != TaskPriority::kBackground) {
    return absl::InvalidArgumentError("invalid task priority");
  }
  return runner_;
}

absl::Status FakePort::Log(LogLevel level, std::string_view redacted_message) {
  PortCallScope const scope;
  if (level != LogLevel::kVerbose && level != LogLevel::kDebug &&
      level != LogLevel::kInfo && level != LogLevel::kWarning &&
      level != LogLevel::kError) {
    return absl::InvalidArgumentError("invalid log level");
  }
  std::scoped_lock const lock(hooks_mutex_);
  logs_.push_back({level, std::string(redacted_message)});
  return absl::OkStatus();
}

absl::Status FakePort::SetMemoryPressureObserver(
    std::shared_ptr<MemoryPressureObserver> observer) {
  PortCallScope const scope;
  std::scoped_lock const lock(hooks_mutex_);
  if (memory_callbacks_ != 0) {
    return absl::UnavailableError("memory pressure callback is in flight");
  }
  memory_observer_ = std::move(observer);
  return absl::OkStatus();
}

absl::Status FakePort::SignalMemoryPressure(MemoryPressure pressure) {
  std::shared_ptr<MemoryPressureObserver> observer;
  {
    std::scoped_lock const lock(hooks_mutex_);
    if (memory_observer_ == nullptr) {
      return absl::FailedPreconditionError(
          "memory pressure observer is absent");
    }
    observer = memory_observer_;
    ++memory_callbacks_;
  }
  CallbackScope const scope;
  auto status = observer->OnMemoryPressure(pressure);
  {
    std::scoped_lock const lock(hooks_mutex_);
    --memory_callbacks_;
  }
  return status;
}

std::vector<FakeLogEntry> FakePort::Logs() const {
  std::scoped_lock const lock(hooks_mutex_);
  return logs_;
}

}
