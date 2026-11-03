#ifndef UMBRA_PIPELINE_PLAYBACK_STATE_MACHINE_H_
#define UMBRA_PIPELINE_PLAYBACK_STATE_MACHINE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "penumbra/interface/callback_dispatcher.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/task_runner.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/sequence_checker.h"
#include "umbra/pipeline/buffer_manager.h"
#include "umbra/pipeline/event_queue.h"
#include "umbra/pipeline/media_events.h"
#include "umbra/pipeline/memory_profile.h"
#include "umbra/pipeline/playback_state.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/player/media_state.h"

namespace penumbra {
class Port;
}

namespace umbra::pipeline {

class PlaybackStateMachine {
 public:
  static constexpr int kMinPrerollMs = 0;
  static constexpr int kMaxPrerollMs = 10000;
  static constexpr int kDefaultPrerollMs = 500;
  static constexpr int kMinTimeUpdateIntervalMs = 15;
  static constexpr int kMaxTimeUpdateIntervalMs = 250;
  static constexpr int kProgressIntervalMs = 350;
  static constexpr int kStalledAfterMs = 3000;

  struct Config {
    int preroll_ms = kDefaultPrerollMs;
    int max_recreate_attempts = 2;
    int timeupdate_interval_ms = kMaxTimeUpdateIntervalMs;
    FastSeekSnap fast_seek_snap = FastSeekSnap::kPrevious;
    BufferManager::Config buffer;
    MemoryProfileMode memory_profile_mode = MemoryProfileMode::kAuto;
    MemoryProfileSignals memory_profile_signals;
    int low_memory_max_video_height = 1080;
    std::function<bool()> allow_playback;
    StateTracker::Observer on_transition;
  };

  struct BufferedRange {
    penumbra::MediaTime start;
    penumbra::MediaTime end;
  };

  struct MediaError {
    MediaErrorCode code = MediaErrorCode::kNone;
    base::ErrorDetail detail;
  };

  static absl::Status ValidateConfig(const Config& config);

  PlaybackStateMachine(penumbra::PlayerFactory& factory,
                       std::shared_ptr<penumbra::ControlTaskRunner> control,
                       Config config);
  PlaybackStateMachine(const PlaybackStateMachine&) = delete;
  PlaybackStateMachine& operator=(const PlaybackStateMachine&) = delete;
  ~PlaybackStateMachine();

  void Load(std::unique_ptr<SampleSource> source);
  void Play(PlayCallback done);
  void Pause();
  absl::Status Seek(penumbra::MediaTime position);
  absl::Status FastSeek(penumbra::MediaTime position);
  absl::Status SetPlaybackRate(double rate);
  absl::Status SetDefaultPlaybackRate(double rate);
  absl::Status SetVolume(double volume);
  absl::Status SetMuted(bool muted);
  void SetLoop(bool loop) { loop_ = loop; }
  void SetAutoplay(bool autoplay) { autoplay_ = autoplay; }
  void SetPreload(player::Preload preload);
  [[nodiscard]] absl::Status SetBufferConfig(BufferManager::Config config);
  [[nodiscard]] int MaxVideoHeight() const {
    return memory_profile_.MaxVideoHeight();
  }
  void Shutdown();

  [[nodiscard]] PlaybackState GetState() const { return tracker_.GetState(); }
  [[nodiscard]] NetworkState GetNetworkState() const { return network_; }
  [[nodiscard]] ReadyState GetReadyState() const { return ready_; }
  [[nodiscard]] bool IsPaused() const { return paused_; }
  [[nodiscard]] bool IsSeeking() const { return seeking_; }
  [[nodiscard]] bool IsEnded() const { return playback_ended_; }
  [[nodiscard]] bool GetLoop() const { return loop_; }
  [[nodiscard]] bool GetAutoplay() const { return autoplay_; }
  [[nodiscard]] player::Preload GetPreload() const { return preload_; }
  [[nodiscard]] double GetPlaybackRate() const { return playback_rate_; }
  [[nodiscard]] double GetDefaultPlaybackRate() const { return default_rate_; }
  [[nodiscard]] double GetVolume() const { return volume_; }
  [[nodiscard]] bool IsMuted() const { return muted_; }
  [[nodiscard]] penumbra::MediaTime GetDuration() const { return duration_; }
  [[nodiscard]] const std::optional<MediaError>& GetError() const {
    return error_;
  }
  [[nodiscard]] penumbra::MediaTime CurrentTime();
  [[nodiscard]] std::optional<BufferedRange> GetBufferedRange() const;
  [[nodiscard]] MediaElementState Snapshot() const;
  EventQueue& Events() { return events_; }

 private:
  enum class PortState : uint8_t { kNone, kReady, kPresenting, kEnded };

  struct TrackState {
    uint32_t track_id = 0;
    penumbra::MediaTime buffered_end;
    bool eos_written = false;
    size_t grant = 0;
  };

  struct Anchor {
    PlaybackStateMachine* machine = nullptr;
  };

  class PortReceiver;
  class SourceReceiver;
  class PressureReceiver;
  friend class PortReceiver;
  friend class SourceReceiver;

  enum class TimerKind : uint8_t { kTimeUpdate, kProgress, kStalled };

  void Fire(MediaEventType type,
            std::optional<base::ErrorDetail> detail = std::nullopt);
  void ArmTimer(TimerKind kind, absl::Duration delay);
  void OnTimer(TimerKind kind, uint64_t epoch);
  void UpdateTimers();
  void ResetTimers();
  void ResumeDeferredOpen();
  [[nodiscard]] bool Fetching() const;
  void PushCompletion(PlayCallback callback, PlayResultCode code,
                      std::optional<base::ErrorDetail> detail = std::nullopt);
  void NotifyAboutPlaying();
  void CompletePendingPlays(PlayResultCode code,
                            const std::optional<base::ErrorDetail>& detail);

  void AbortAndReset();
  void SelectResource(std::unique_ptr<SampleSource> source);
  void InternalPlay();
  void InternalPause();
  void RunSeek(penumbra::MediaTime target, SeekMode mode = SeekMode::kAccurate);
  void CompleteSeek();
  void HandleEndOfPlayback();

  void SetReadyState(ReadyState next);
  [[nodiscard]] ReadyState ComputeReadyState() const;
  [[nodiscard]] bool PotentiallyPlaying() const;
  void Reevaluate();
  void RefreshPosition();
  void UpdateState();
  [[nodiscard]] PlaybackState DeriveState() const;
  absl::Status SyncPortRate();
  void SyncPortRateOrFail();
  [[nodiscard]] double DesiredPortRate() const;
  [[nodiscard]] double EffectiveVolume() const;
  [[nodiscard]] bool PlaybackAllowed() const;

  absl::Status CreatePort();
  absl::Status IssueSeek(penumbra::MediaTime position,
                         SeekMode mode = SeekMode::kAccurate);
  void RebuildPort();
  void StopPort();
  void TearDownPipeline();
  void ResetTracks(penumbra::MediaTime position);

  void OnSourceOpened(SourceDescription description);
  void OnSourceFailed(base::ErrorDetail detail);
  void OnSourceDuration(penumbra::MediaTime duration);
  void OnSourceSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                       std::vector<penumbra::EncodedSample> samples);
  void OnSourceEndOfStream(penumbra::SeekTicket ticket, uint32_t track_id);
  void OnSourceError(base::ErrorDetail detail);

  void OnPortEvent(penumbra::PlayerEvent event);
  void OnNeedsData(const penumbra::NeedsData& request);
  void OnMemoryPressure(penumbra::MemoryPressure pressure);
  void OnPortState(const penumbra::StateChanged& change);
  void HandlePortFailure(const penumbra::PortError& error);
  void FailFatal(base::ErrorDetail detail);
  TrackState* FindTrack(uint32_t track_id);

  penumbra::PlayerFactory& factory_;
  std::shared_ptr<penumbra::ControlTaskRunner> control_;
  Config config_;
  BufferManager buffer_manager_;
  MemoryProfilePolicy memory_profile_;
  penumbra::Port* port_ = nullptr;
  std::shared_ptr<PressureReceiver> pressure_receiver_;
  base::SequenceChecker sequence_;
  EventQueue events_;
  StateTracker tracker_;

  std::unique_ptr<SampleSource> source_;
  uint64_t source_generation_ = 0;
  std::unique_ptr<penumbra::Player> player_;
  std::shared_ptr<penumbra::CallbackDispatcher> dispatcher_;
  uint64_t port_generation_ = 0;
  penumbra::SeekTicket ticket_ = 0;
  SourceDescription description_;
  std::vector<TrackState> tracks_;

  NetworkState network_ = NetworkState::kEmpty;
  ReadyState ready_ = ReadyState::kHaveNothing;
  PortState port_state_ = PortState::kNone;
  bool paused_ = true;
  bool seeking_ = false;
  bool playback_ended_ = false;
  bool opened_ = false;
  bool prerolled_ = false;
  bool loaded_data_fired_ = false;
  bool fetch_complete_ = false;
  bool can_autoplay_ = true;
  bool autoplay_ = false;
  bool loop_ = false;
  bool underflowed_ = false;
  bool rate_changed_since_presenting_ = true;
  bool shut_down_ = false;
  player::Preload preload_ = player::Preload::kMetadata;
  bool deferred_open_ = false;
  uint64_t timer_epoch_ = 0;
  bool timeupdate_armed_ = false;
  bool progress_armed_ = false;
  bool stall_armed_ = false;
  bool data_since_progress_ = false;
  bool data_since_stall_check_ = false;
  bool stalled_fired_ = false;
  double playback_rate_ = 1.0;
  double default_rate_ = 1.0;
  double applied_rate_ = 0.0;
  double volume_ = 1.0;
  bool muted_ = false;
  int recreate_attempts_ = 0;
  penumbra::MediaTime position_ = penumbra::MediaTime::Zero();
  penumbra::MediaTime buffered_start_ = penumbra::MediaTime::Zero();
  penumbra::MediaTime duration_ = penumbra::MediaTime::Unknown();
  std::optional<penumbra::MediaTime> default_start_position_;
  SeekMode default_start_mode_ = SeekMode::kAccurate;
  std::optional<MediaError> error_;
  std::vector<PlayCallback> pending_plays_;

  std::shared_ptr<Anchor> anchor_ = std::make_shared<Anchor>(Anchor{this});
};

}

#endif
