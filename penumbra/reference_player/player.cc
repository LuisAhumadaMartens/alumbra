#include "penumbra/reference_player/player.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "penumbra/interface/callback_dispatcher.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
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
#include "penumbra/reference_player/audio_converter_provider.h"
#include "penumbra/reference_player/decoder_chain.h"
#include "penumbra/reference_player/media_clock.h"
#include "penumbra/reference_player/video_frame_selector.h"

namespace penumbra {
namespace {

int64_t MonotonicMicros() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::string CapabilityMimeType(const StreamInfo& stream) {
  const std::string_view media_type =
      stream.type() == StreamType::kVideo ? "video/mp4"
      : stream.codec == CodecId::kMp3     ? "audio/mpeg"
                                          : "audio/mp4";
  return absl::StrCat(media_type, "; codecs=\"", stream.codec_string, "\"");
}

class FrameQueue final : public VideoFrameSource {
 public:
  static constexpr size_t kCapacity = 16;

  FrameQueue() : slots_(kCapacity), selection_frames_(kCapacity) {}

  void Push(DecodedVideoFrame frame) {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t tail = tail_.load(std::memory_order_acquire);
    if (head - tail == kCapacity) {
      return;
    }
    slots_[head % kCapacity] = frame;
    latest_ = std::move(frame);
    head_.store(head + 1, std::memory_order_release);
  }

  std::optional<DecodedVideoFrame> TakeDueFrame(
      MediaTime time) noexcept override {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    const size_t head = head_.load(std::memory_order_acquire);
    const size_t count = std::min(head - tail, kCapacity);
    if (count == 0) {
      return std::nullopt;
    }
    for (size_t index = 0; index < count; ++index) {
      selection_frames_[index] = slots_[(tail + index) % kCapacity];
    }
    const VideoFrameSelection selection =
        selector_.Select({selection_frames_.data(), count}, time);
    if (!selection.frame_index.has_value()) {
      if (selection.dropped_frames == count) {
        size_t expected_tail = tail;
        static_cast<void>(tail_.compare_exchange_strong(
            expected_tail, tail + count, std::memory_order_release,
            std::memory_order_relaxed));
      }
      dropped_frames_.store(selector_.DroppedFrames(),
                            std::memory_order_relaxed);
      return std::nullopt;
    }
    const size_t selected = *selection.frame_index;
    DecodedVideoFrame frame = std::move(selection_frames_[selected]);
    size_t expected_tail = tail;
    if (!tail_.compare_exchange_strong(expected_tail, tail + selected + 1,
                                       std::memory_order_release,
                                       std::memory_order_relaxed)) {
      return std::nullopt;
    }
    dropped_frames_.store(selector_.DroppedFrames(), std::memory_order_relaxed);
    return frame;
  }

  std::optional<DecodedVideoFrame> Current() const { return latest_; }

  void Clear() {
    size_t tail = tail_.load(std::memory_order_relaxed);
    const size_t head = head_.load(std::memory_order_acquire);
    while (tail < head &&
           !tail_.compare_exchange_weak(tail, head, std::memory_order_release,
                                        std::memory_order_relaxed)) {
    }
    latest_.reset();
    selector_.Reset();
    dropped_frames_.store(0, std::memory_order_relaxed);
  }

  uint64_t DroppedFrames() const {
    return dropped_frames_.load(std::memory_order_relaxed);
  }

 private:
  std::vector<DecodedVideoFrame> slots_;
  std::vector<DecodedVideoFrame> selection_frames_;
  std::atomic<size_t> head_{0};
  std::atomic<size_t> tail_{0};
  std::optional<DecodedVideoFrame> latest_;
  VideoFrameSelector selector_;
  std::atomic<uint64_t> dropped_frames_{0};
};

class PcmRing final : public AudioSource {
 public:
  explicit PcmRing(size_t channels)
      : channels_(channels), samples_(channels * 48000 * 4) {}

  void Push(std::span<const float> samples) noexcept {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t tail = tail_.load(std::memory_order_acquire);
    const size_t available = samples_.size() - (head - tail);
    const size_t count = std::min(samples.size(), available) -
                         (std::min(samples.size(), available) % channels_);
    for (size_t index = 0; index < count; ++index) {
      samples_[(head + index) % samples_.size()] = samples[index];
    }
    head_.store(head + count, std::memory_order_release);
  }

  size_t Read(std::span<uint8_t> destination) noexcept override {
    const size_t frame_bytes = channels_ * sizeof(float);
    const size_t wanted =
        destination.size() - (destination.size() % frame_bytes);
    const size_t tail = tail_.load(std::memory_order_relaxed);
    const size_t head = head_.load(std::memory_order_acquire);
    const size_t available = (head - tail) * sizeof(float);
    const size_t count =
        std::min(wanted, available - (available % frame_bytes));
    const size_t samples = count / sizeof(float);
    for (size_t index = 0; index < samples; ++index) {
      const float sample = samples_[(tail + index) % samples_.size()];
      std::memcpy(
          destination.subspan(index * sizeof(float), sizeof(float)).data(),
          &sample, sizeof(float));
    }
    tail_.store(tail + samples, std::memory_order_release);
    return count;
  }

  void Clear() { tail_.store(head_.load(std::memory_order_acquire)); }

 private:
  size_t channels_;
  std::vector<float> samples_;
  std::atomic<size_t> head_{0};
  std::atomic<size_t> tail_{0};
};

class PlayerCore final : public Player,
                         public std::enable_shared_from_this<PlayerCore> {
 public:
  PlayerCore(PlayerConfig config, std::shared_ptr<PlayerCallbacks> callbacks,
             Port& port, std::shared_ptr<ControlTaskRunner> control)
      : config_(std::move(config)),
        callbacks_(std::move(callbacks)),
        port_(port),
        control_(std::move(control)),
        ticket_(config_.initial_ticket) {}

  absl::Status Initialise();
  absl::Status WriteSamples(SeekTicket ticket, uint32_t track_id,
                            std::span<const EncodedSample> samples) override;
  absl::Status WriteFragment(SeekTicket ticket, uint32_t requested_track,
                             const MediaFragment& fragment) override;
  absl::Status EndOfStream(SeekTicket ticket, uint32_t track_id) override;
  absl::Status Seek(SeekTicket ticket, MediaTime position) override;
  absl::Status Flush(SeekTicket ticket) override;
  absl::Status SetPlaybackRate(double rate) override;
  absl::Status SetVolume(double volume) override;
  absl::Status SetBounds(uint32_t track_id, OutputBounds bounds) override;
  absl::StatusOr<PlaybackSnapshot> GetSnapshot() const override;
  absl::StatusOr<std::optional<DecodedVideoFrame>> GetCurrentFrame(
      uint32_t track_id) const override;
  absl::Status Stop() override;

 private:
  struct Track {
    StreamConfig stream;
    std::unique_ptr<DecoderChain> decoder;
    std::unique_ptr<VideoSink> video_sink;
    std::unique_ptr<AudioSink> audio_sink;
    std::shared_ptr<FrameQueue> frames;
    std::shared_ptr<PcmRing> audio;
    bool granted = false;
    size_t grant_max = 0;
    bool ended = false;
    bool eos_notified = false;
    bool needs_data_pending = false;
    std::optional<DecodedVideoFrame> current_frame;
  };

  class DecoderReceiver final : public DecoderCallbacks {
   public:
    DecoderReceiver(std::weak_ptr<PlayerCore> player, uint32_t track_id)
        : player_(std::move(player)), track_id_(track_id) {}
    absl::Status OnDecoded(DecoderEvent event) override;

   private:
    std::weak_ptr<PlayerCore> player_;
    uint32_t track_id_;
  };

  absl::Status Check() const;
  Track* Find(uint32_t track_id);
  const Track* Find(uint32_t track_id) const;
  absl::Status Deliver(PlayerEvent event);
  void OnDecoderEvent(uint32_t track_id, DecoderEvent event);
  void HandleNeedsData(Track& track, const NeedsData& needs);
  void HandleVideoFrame(Track& track, DecodedVideoFrame frame);
  void HandleAudioBuffer(Track& track, const DecodedAudioBuffer& buffer);
  void HandleStateChanged(Track& track, const StateChanged& state);
  absl::Status InitialiseTrack(StreamConfig& stream);
  absl::Status InitialiseDecoders();
  absl::Status ValidateSample(const Track& track,
                              const EncodedSample& sample) const;
  void ResetGeneration(SeekTicket ticket, bool reset_clock, MediaTime position);

  PlayerConfig config_;
  std::shared_ptr<PlayerCallbacks> callbacks_;
  Port& port_;
  std::shared_ptr<ControlTaskRunner> control_;
  std::vector<Track> tracks_;
  SeekTicket ticket_;
  double rate_ = 1.0;
  double volume_ = 1.0;
  bool stopped_ = false;
  bool playback_active_ = false;
  bool aggregate_eos_ = false;
  PlaybackSnapshot snapshot_;
  MediaClock clock_;
  ReferenceAudioConverterProvider* audio_converter_ = nullptr;
};

absl::Status PlayerCore::Check() const {
  assert(control_->RunsTasksInCurrentSequence());
  return stopped_ ? absl::FailedPreconditionError("player is stopped")
                  : absl::OkStatus();
}

PlayerCore::Track* PlayerCore::Find(uint32_t track_id) {
  const auto it = std::ranges::find_if(tracks_, [track_id](const Track& track) {
    return track.stream.info.track_id == track_id;
  });
  return it == tracks_.end() ? nullptr : &*it;
}

const PlayerCore::Track* PlayerCore::Find(uint32_t track_id) const {
  const auto it = std::ranges::find_if(tracks_, [track_id](const Track& track) {
    return track.stream.info.track_id == track_id;
  });
  return it == tracks_.end() ? nullptr : &*it;
}

absl::Status PlayerCore::Deliver(PlayerEvent event) {
  return control_->PostTask(
      [weak = weak_from_this(), event = std::move(event)]() mutable {
        auto player = weak.lock();
        if (player == nullptr || player->stopped_ ||
            event.ticket != player->ticket_ || player->callbacks_ == nullptr) {
          return;
        }
        CallbackScope const scope;
        static_cast<void>(player->callbacks_->OnEvent(std::move(event)));
      },
      absl::ZeroDuration());
}

absl::Status PlayerCore::DecoderReceiver::OnDecoded(DecoderEvent event) {
  auto player = player_.lock();
  if (player == nullptr) {
    return absl::OkStatus();
  }
  return player->control_->PostTask(
      [weak = player_, track_id = track_id_,
       event = std::move(event)]() mutable {
        if (auto current = weak.lock()) {
          current->OnDecoderEvent(track_id, std::move(event));
        }
      },
      absl::ZeroDuration());
}

void PlayerCore::OnDecoderEvent(uint32_t track_id, DecoderEvent event) {
  if (stopped_ || event.ticket != ticket_) {
    return;
  }
  Track* track = Find(track_id);
  if (track == nullptr) {
    return;
  }
  if (const auto* needs = std::get_if<NeedsData>(&event.detail)) {
    HandleNeedsData(*track, *needs);
    return;
  }
  if (auto* frame = std::get_if<DecodedVideoFrame>(&event.detail)) {
    HandleVideoFrame(*track, std::move(*frame));
    return;
  }
  if (auto* audio = std::get_if<DecodedAudioBuffer>(&event.detail)) {
    HandleAudioBuffer(*track, *audio);
    return;
  }
  if (const auto* state = std::get_if<StateChanged>(&event.detail)) {
    HandleStateChanged(*track, *state);
    return;
  }
  if (const auto* error = std::get_if<PortError>(&event.detail)) {
    static_cast<void>(Deliver({.ticket = ticket_, .detail = *error}));
  }
}

void PlayerCore::HandleNeedsData(Track& track, const NeedsData& needs) {
  if (track.ended || track.granted || needs.max_samples == 0) {
    return;
  }
  if (!playback_active_ && !config_.allow_preroll) {
    track.needs_data_pending = true;
    track.grant_max = needs.max_samples;
    return;
  }
  track.needs_data_pending = false;
  track.granted = true;
  track.grant_max = needs.max_samples;
  static_cast<void>(Deliver({.ticket = ticket_, .detail = needs}));
}

void PlayerCore::HandleVideoFrame(Track& track, DecodedVideoFrame frame) {
  ++snapshot_.frames.decoded;
  track.frames->Push(std::move(frame));
  auto current = track.frames->Current();
  if (current.has_value()) {
    track.current_frame = std::move(*current);
  }
}

void PlayerCore::HandleAudioBuffer(Track& track,
                                   const DecodedAudioBuffer& buffer) {
  ++snapshot_.frames.decoded;
  if (audio_converter_ == nullptr || track.audio == nullptr) {
    return;
  }
  auto converted = audio_converter_->ConvertAudio(track.stream, buffer);
  if (converted.ok()) {
    track.audio->Push(*converted);
    return;
  }
  static_cast<void>(
      Deliver({.ticket = ticket_,
               .detail = PortError{.status = converted.status(),
                                   .category = PortErrorCategory::kOutput,
                                   .recovery = RecoveryHint::kFatal}}));
}

void PlayerCore::HandleStateChanged(Track& track, const StateChanged& state) {
  if (state.state != PlayerState::kEndOfStream) {
    return;
  }
  track.eos_notified = true;
  static_cast<void>(Deliver({.ticket = ticket_, .detail = state}));
  const bool all_drained = std::ranges::all_of(
      tracks_, [](const Track& value) { return value.eos_notified; });
  if (!all_drained || aggregate_eos_) {
    return;
  }
  aggregate_eos_ = true;
  static_cast<void>(
      Deliver({.ticket = ticket_,
               .detail = StateChanged{.state = PlayerState::kEndOfStream,
                                      .track_id = std::nullopt}}));
}

absl::Status PlayerCore::Initialise() {
  if (!control_->RunsTasksInCurrentSequence()) {
    return absl::FailedPreconditionError(
        "player creation requires control sequence");
  }
  if (config_.input_mode == InputMode::kFragments) {
    return absl::UnimplementedError(
        "the reference player does not transform fragments");
  }
  audio_converter_ = dynamic_cast<ReferenceAudioConverterProvider*>(&port_);
  for (auto& stream : config_.streams) {
    auto status = InitialiseTrack(stream);
    if (!status.ok()) {
      return status;
    }
  }
  return InitialiseDecoders();
}

absl::Status PlayerCore::InitialiseTrack(StreamConfig& stream) {
  CapabilityRequest request;
  request.stream = stream.info;
  request.mime_type = CapabilityMimeType(stream.info);
  request.encryption_scheme = stream.encryption_scheme;
  auto capability = port_.QueryDecoding(request);
  if (!capability.ok()) {
    return capability.status();
  }
  if (!capability->supported) {
    return absl::UnimplementedError("stream codec is unsupported");
  }
  Track track;
  track.stream = stream;
  if (stream.info.type() == StreamType::kVideo) {
    track.frames = std::make_shared<FrameQueue>();
    auto sink = port_.CreateVideoSink(stream.output_mode);
    if (!sink.ok()) {
      return sink.status();
    }
    auto status = (*sink)->Start(track.frames);
    if (!status.ok()) {
      return status;
    }
    track.video_sink = std::move(*sink);
  } else if (stream.info.type() == StreamType::kAudio) {
    const auto& audio = std::get<AudioInfo>(stream.info.format);
    track.audio = std::make_shared<PcmRing>(
        static_cast<size_t>(std::max(audio.channel_count, 1)));
    auto sink = port_.CreateAudioSink();
    if (!sink.ok()) {
      return sink.status();
    }
    AudioInfo output = audio;
    output.sample_format = SampleFormat::kF32;
    auto status = (*sink)->Start(output, track.audio);
    if (!status.ok()) {
      return status;
    }
    track.audio_sink = std::move(*sink);
  }
  tracks_.push_back(std::move(track));
  return absl::OkStatus();
}

absl::Status PlayerCore::InitialiseDecoders() {
  const std::weak_ptr<PlayerCore> weak = weak_from_this();
  for (auto& track : tracks_) {
    if (track.stream.info.type() == StreamType::kText) {
      continue;
    }
    auto receiver =
        std::make_shared<DecoderReceiver>(weak, track.stream.info.track_id);
    DecoderChainConfig chain_config;
    chain_config.factory = &port_;
    chain_config.capabilities = &port_;
    chain_config.control = control_;
    chain_config.downstream = std::move(receiver);
    chain_config.stream = track.stream;
    chain_config.capability_request.stream = track.stream.info;
    chain_config.capability_request.mime_type =
        CapabilityMimeType(track.stream.info);
    chain_config.capability_request.encryption_scheme =
        track.stream.encryption_scheme;
    chain_config.settings = config_.decoder;
    auto chain = DecoderChain::Create(std::move(chain_config), ticket_);
    if (!chain.ok()) {
      return chain.status();
    }
    track.decoder = std::move(*chain);
  }
  return absl::OkStatus();
}

absl::Status PlayerCore::ValidateSample(const Track& track,
                                        const EncodedSample& sample) const {
  const uint32_t track_id = track.stream.info.track_id;
  if (sample.track_id != track_id || sample.data.empty() ||
      !ToMediaTime(sample.pts, Rounding::kNearest).has_value()) {
    return absl::InvalidArgumentError("invalid sample track or timestamp");
  }
  if (ValidateEncryption(sample) != EncryptionCheck::kValid ||
      (sample.encryption.has_value() &&
       track.stream.encryption_scheme != sample.encryption->scheme)) {
    return absl::InvalidArgumentError("invalid encryption metadata");
  }
  if (sample.config_change == nullptr) {
    return absl::OkStatus();
  }
  if (sample.config_change->track_id != track_id) {
    return absl::InvalidArgumentError(
        "configuration change cannot change track identity");
  }
  auto can_change = port_.CanChangeType(
      track.stream,
      StreamConfig{.info = *sample.config_change,
                   .encryption_scheme = track.stream.encryption_scheme,
                   .output_mode = track.stream.output_mode});
  if (!can_change.ok()) {
    return can_change.status();
  }
  if (!*can_change) {
    return absl::UnimplementedError("stream reconfiguration is unsupported");
  }
  return absl::OkStatus();
}

absl::Status PlayerCore::WriteSamples(SeekTicket ticket, uint32_t track_id,
                                      std::span<const EncodedSample> samples) {
  PortCallScope const scope;
  auto status = Check();
  if (!status.ok()) {
    return status;
  }
  if (ticket < ticket_) {
    return absl::OkStatus();
  }
  if (ticket > ticket_) {
    return absl::InvalidArgumentError("future ticket");
  }
  Track* track = Find(track_id);
  if (track == nullptr || samples.empty()) {
    return absl::InvalidArgumentError("invalid sample write");
  }
  if (config_.input_mode != InputMode::kSamples) {
    return absl::InvalidArgumentError("player uses fragments");
  }
  if (track->ended) {
    return absl::FailedPreconditionError("track has ended");
  }
  if (!track->granted) {
    return absl::FailedPreconditionError("write has no grant");
  }
  if (samples.size() > track->grant_max) {
    return absl::InvalidArgumentError("sample batch exceeds its grant");
  }
  for (const auto& sample : samples) {
    status = ValidateSample(*track, sample);
    if (!status.ok()) {
      return status;
    }
  }
  status = track->decoder->WriteSamples(ticket_, samples);
  if (!status.ok()) {
    return status;
  }
  track->granted = false;
  track->grant_max = 0;
  if (samples.front().config_change != nullptr) {
    track->stream.info = *samples.front().config_change;
  }
  return absl::OkStatus();
}

absl::Status PlayerCore::WriteFragment(SeekTicket ticket,
                                       uint32_t requested_track,
                                       const MediaFragment& fragment) {
  PortCallScope const scope;
  auto status = Check();
  if (!status.ok()) {
    return status;
  }
  if (ticket < ticket_) {
    return absl::OkStatus();
  }
  if (ticket != ticket_ || config_.input_mode != InputMode::kFragments ||
      fragment.track_ids.empty() || fragment.data.empty() ||
      std::ranges::find(fragment.track_ids, requested_track) ==
          fragment.track_ids.end() ||
      Find(requested_track) == nullptr) {
    return absl::InvalidArgumentError("invalid media fragment");
  }
  return absl::UnimplementedError(
      "the reference player does not transform fragments");
}

absl::Status PlayerCore::EndOfStream(SeekTicket ticket, uint32_t track_id) {
  PortCallScope const scope;
  auto status = Check();
  if (!status.ok()) {
    return status;
  }
  if (ticket < ticket_) {
    return absl::OkStatus();
  }
  if (ticket > ticket_) {
    return absl::InvalidArgumentError("future ticket");
  }
  Track* track = Find(track_id);
  if (track == nullptr) {
    return absl::InvalidArgumentError("unknown track");
  }
  if (!track->granted || track->ended) {
    return absl::FailedPreconditionError("end of stream has no grant");
  }
  if (track->decoder != nullptr) {
    status = track->decoder->EndOfStream(ticket_);
    if (!status.ok()) {
      return status;
    }
  }
  track->granted = false;
  track->grant_max = 0;
  track->ended = true;
  if (track->decoder == nullptr) {
    track->eos_notified = true;
    status = Deliver({.ticket = ticket_,
                      .detail = StateChanged{.state = PlayerState::kEndOfStream,
                                             .track_id = track_id}});
    if (!status.ok()) {
      return status;
    }
    const bool all_drained = std::ranges::all_of(
        tracks_, [](const Track& value) { return value.eos_notified; });
    if (all_drained && !aggregate_eos_) {
      aggregate_eos_ = true;
      return Deliver({.ticket = ticket_,
                      .detail = StateChanged{.state = PlayerState::kEndOfStream,
                                             .track_id = std::nullopt}});
    }
  }
  return absl::OkStatus();
}

void PlayerCore::ResetGeneration(SeekTicket ticket, bool reset_clock,
                                 MediaTime position) {
  ticket_ = ticket;
  aggregate_eos_ = false;
  snapshot_.frames = {};
  if (audio_converter_ != nullptr) {
    audio_converter_->ResetAudioConverters();
  }
  for (auto& track : tracks_) {
    track.granted = false;
    track.grant_max = 0;
    track.ended = false;
    track.eos_notified = false;
    track.current_frame.reset();
    if (track.frames != nullptr) {
      track.frames->Clear();
    }
    if (track.audio != nullptr) {
      track.audio->Clear();
    }
    if (track.decoder != nullptr) {
      static_cast<void>(track.decoder->Flush(ticket));
    }
  }
  if (reset_clock) {
    clock_.Reset(position, MonotonicMicros());
  }
}

absl::Status PlayerCore::Seek(SeekTicket ticket, MediaTime position) {
  PortCallScope const scope;
  auto status = Check();
  if (!status.ok()) {
    return status;
  }
  if (ticket == 0 || ticket <= ticket_ || !position.is_finite()) {
    return absl::InvalidArgumentError("invalid seek");
  }
  ResetGeneration(ticket, true, position);
  return absl::OkStatus();
}

absl::Status PlayerCore::Flush(SeekTicket ticket) {
  PortCallScope const scope;
  auto status = Check();
  if (!status.ok()) {
    return status;
  }
  if (ticket == 0 || ticket <= ticket_) {
    return absl::InvalidArgumentError("invalid flush ticket");
  }
  const MediaTime position = clock_.Now(MonotonicMicros());
  ResetGeneration(ticket, true, position);
  if (playback_active_) {
    clock_.SetRate(rate_, MonotonicMicros());
    clock_.SetPaused(rate_ == 0, MonotonicMicros());
  }
  return absl::OkStatus();
}

absl::Status PlayerCore::SetPlaybackRate(double rate) {
  PortCallScope const scope;
  auto status = Check();
  if (!status.ok()) {
    return status;
  }
  if (!std::isfinite(rate) || rate < 0) {
    return absl::InvalidArgumentError("invalid playback rate");
  }
  const bool was_active = playback_active_;
  rate_ = rate;
  if (audio_converter_ != nullptr) {
    status = audio_converter_->SetAudioRate(rate);
    if (!status.ok()) {
      return status;
    }
  }
  clock_.SetRate(rate, MonotonicMicros());
  clock_.SetPaused(rate == 0, MonotonicMicros());
  playback_active_ = rate > 0;
  if (was_active != playback_active_) {
    static_cast<void>(
        Deliver({.ticket = ticket_,
                 .detail = StateChanged{.state = playback_active_
                                                     ? PlayerState::kPresenting
                                                     : PlayerState::kReady,
                                        .track_id = std::nullopt}}));
  }
  for (auto& track : tracks_) {
    if (track.audio_sink != nullptr &&
        track.stream.info.type() == StreamType::kAudio) {
      static_cast<void>(track.audio_sink->GetOutputLatency());
    }
    if (playback_active_ && track.needs_data_pending) {
      track.needs_data_pending = false;
      track.granted = true;
      static_cast<void>(
          Deliver({.ticket = ticket_,
                   .detail = NeedsData{.track_id = track.stream.info.track_id,
                                       .max_samples = track.grant_max}}));
    }
  }
  return absl::OkStatus();
}

absl::Status PlayerCore::SetVolume(double volume) {
  PortCallScope const scope;
  auto status = Check();
  if (!status.ok()) {
    return status;
  }
  if (!std::isfinite(volume) || volume < 0 || volume > 1) {
    return absl::InvalidArgumentError("volume must be in [0, 1]");
  }
  volume_ = volume;
  if (audio_converter_ != nullptr) {
    status = audio_converter_->SetAudioVolume(volume);
    if (!status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

absl::Status PlayerCore::SetBounds(uint32_t track_id, OutputBounds bounds) {
  PortCallScope const scope;
  auto status = Check();
  if (!status.ok()) {
    return status;
  }
  Track* track = Find(track_id);
  if (track == nullptr || track->stream.info.type() != StreamType::kVideo ||
      track->stream.output_mode != OutputMode::kPunchOut || bounds.width < 0 ||
      bounds.height < 0) {
    return absl::InvalidArgumentError("invalid video bounds");
  }
  return track->video_sink->SetBounds(bounds);
}

absl::StatusOr<PlaybackSnapshot> PlayerCore::GetSnapshot() const {
  PortCallScope const scope;
  auto status = Check();
  if (!status.ok()) {
    return status;
  }
  PlaybackSnapshot result = snapshot_;
  result.media_time = clock_.Now(MonotonicMicros());
  for (const auto& track : tracks_) {
    if (track.frames != nullptr) {
      result.frames.dropped += track.frames->DroppedFrames();
    }
    if (track.decoder != nullptr) {
      const DecoderChainStats stats = track.decoder->GetStats();
      result.frames.dropped += stats.Dropped();
      result.frames.corrupted += stats.corrupted;
    }
    if (track.audio_sink != nullptr) {
      auto latency = track.audio_sink->GetOutputLatency();
      if (latency.ok()) {
        result.audio_output_latency = *latency;
      }
      break;
    }
  }
  return result;
}

absl::StatusOr<std::optional<DecodedVideoFrame>> PlayerCore::GetCurrentFrame(
    uint32_t track_id) const {
  PortCallScope const scope;
  auto status = Check();
  if (!status.ok()) {
    return status;
  }
  const Track* track = Find(track_id);
  if (track == nullptr || track->stream.info.type() != StreamType::kVideo ||
      track->stream.output_mode != OutputMode::kTexture) {
    return absl::InvalidArgumentError(
        "current frame requires a texture video track");
  }
  return track->current_frame;
}

absl::Status PlayerCore::Stop() {
  PortCallScope const scope;
  if (stopped_) {
    return absl::OkStatus();
  }
  if (!control_->RunsTasksInCurrentSequence()) {
    return absl::FailedPreconditionError("stop requires control sequence");
  }
  for (auto& track : tracks_) {
    if (track.decoder != nullptr) {
      auto status = track.decoder->Stop();
      if (!status.ok()) {
        return status;
      }
    }
    if (track.video_sink != nullptr) {
      auto status = track.video_sink->Stop();
      if (!status.ok()) {
        return status;
      }
    }
    if (track.audio_sink != nullptr) {
      auto status = track.audio_sink->Stop();
      if (!status.ok()) {
        return status;
      }
    }
  }
  stopped_ = true;
  callbacks_.reset();
  return absl::OkStatus();
}

}

absl::StatusOr<std::unique_ptr<Player>> CreateReferencePlayer(
    PlayerConfig config, std::shared_ptr<PlayerCallbacks> callbacks, Port& port,
    std::shared_ptr<ControlTaskRunner> control) {
  if (config.api_version != kApiVersion || config.initial_ticket == 0 ||
      config.streams.empty() || callbacks == nullptr || control == nullptr) {
    return absl::InvalidArgumentError("invalid player configuration");
  }
  for (size_t i = 0; i < config.streams.size(); ++i) {
    if (std::ranges::any_of(
            config.streams.begin(),
            config.streams.begin() + static_cast<std::ptrdiff_t>(i),
            [&config, i](const StreamConfig& earlier) {
              return earlier.info.track_id == config.streams[i].info.track_id;
            })) {
      return absl::InvalidArgumentError("duplicate track identifier");
    }
  }
  auto player = std::make_shared<PlayerCore>(
      std::move(config), std::move(callbacks), port, std::move(control));
  auto status = player->Initialise();
  if (!status.ok()) {
    static_cast<void>(player->Stop());
    return status;
  }
  class SharedPlayer final : public Player {
   public:
    explicit SharedPlayer(std::shared_ptr<PlayerCore> core)
        : core_(std::move(core)) {}
    absl::Status WriteSamples(SeekTicket ticket, uint32_t track_id,
                              std::span<const EncodedSample> samples) override {
      return core_->WriteSamples(ticket, track_id, samples);
    }
    absl::Status WriteFragment(SeekTicket ticket, uint32_t requested_track,
                               const MediaFragment& fragment) override {
      return core_->WriteFragment(ticket, requested_track, fragment);
    }
    absl::Status EndOfStream(SeekTicket ticket, uint32_t track_id) override {
      return core_->EndOfStream(ticket, track_id);
    }
    absl::Status Seek(SeekTicket ticket, MediaTime position) override {
      return core_->Seek(ticket, position);
    }
    absl::Status Flush(SeekTicket ticket) override {
      return core_->Flush(ticket);
    }
    absl::Status SetPlaybackRate(double rate) override {
      return core_->SetPlaybackRate(rate);
    }
    absl::Status SetVolume(double volume) override {
      return core_->SetVolume(volume);
    }
    absl::Status SetBounds(uint32_t track_id, OutputBounds bounds) override {
      return core_->SetBounds(track_id, bounds);
    }
    absl::StatusOr<PlaybackSnapshot> GetSnapshot() const override {
      return core_->GetSnapshot();
    }
    absl::StatusOr<std::optional<DecodedVideoFrame>> GetCurrentFrame(
        uint32_t track_id) const override {
      return core_->GetCurrentFrame(track_id);
    }
    absl::Status Stop() override { return core_->Stop(); }

   private:
    std::shared_ptr<PlayerCore> core_;
  };
  return std::unique_ptr<Player>(new SharedPlayer(std::move(player)));
}

}
