#ifndef TEST_PENUMBRA_INTERFACE_FAKE_PORT_H_
#define TEST_PENUMBRA_INTERFACE_FAKE_PORT_H_

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "absl/time/time.h"
#include "penumbra/interface/callback_dispatcher.h"
#include "penumbra/interface/port.h"
#include "penumbra_conformance/control_runner_adapter.h"
#include "umbra/base/sequence_checker.h"
#include "umbra/base/task_runner.h"

namespace penumbra::test {

using RunnerAdapter = conformance::ControlRunnerAdapter;

struct FakeFaults {
  bool deliver_outside_callback_scope = false;
  bool ignore_seek_ticket = false;
  bool regress_media_time = false;
};

struct InstanceBudget {
  size_t active = 0;
};

class FakePlayer final : public Player {
 public:
  FakePlayer(PlayerConfig config, std::shared_ptr<PlayerCallbacks> callbacks,
             std::shared_ptr<InstanceBudget> budget, FakeFaults faults = {});
  FakePlayer(const FakePlayer&) = delete;
  FakePlayer& operator=(const FakePlayer&) = delete;
  ~FakePlayer() override;
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
  absl::Status RequestData(uint32_t track_id, size_t max_samples);
  absl::Status ReportError(PortError error);
  absl::Status Advance(absl::Duration elapsed);
  const std::vector<EncodedSample>& Samples() const { return samples_; }
  const std::vector<MediaFragment>& Fragments() const { return fragments_; }

 private:
  absl::Status CheckRunning() const;
  absl::Status AdvanceTicket(SeekTicket ticket);
  absl::Status ConsumeGrant(uint32_t track_id, size_t count);
  const StreamConfig* FindStream(uint32_t track_id) const;
  PlayerConfig config_;
  SeekTicket ticket_;
  std::shared_ptr<PlayerCallbacks> callbacks_;
  std::shared_ptr<InstanceBudget> budget_;
  std::map<uint32_t, size_t> grants_;
  std::map<uint32_t, bool> ended_;
  std::map<uint32_t, OutputBounds> bounds_;
  std::vector<EncodedSample> samples_;
  std::vector<MediaFragment> fragments_;
  PlaybackSnapshot snapshot_;
  FakeFaults faults_;
  double rate_ = 1.0;
  int advances_ = 0;
  bool stopped_ = false;
  bool aggregate_ended_ = false;
  umbra::base::SequenceChecker sequence_;
};

class FakeDecoder final : public Decoder {
 public:
  FakeDecoder(StreamConfig config, SeekTicket ticket,
              std::shared_ptr<DecoderCallbacks> callbacks);
  absl::Status WriteSamples(SeekTicket ticket,
                            std::span<const EncodedSample> samples) override;
  absl::Status EndOfStream(SeekTicket ticket) override;
  absl::Status Flush(SeekTicket ticket) override;
  absl::Status Stop() override;
  absl::Status RequestData(size_t count);

 private:
  absl::Status CheckRunning() const;
  StreamConfig config_;
  SeekTicket ticket_;
  std::shared_ptr<DecoderCallbacks> callbacks_;
  size_t grant_ = 0;
  bool ended_ = false;
  bool stopped_ = false;
  umbra::base::SequenceChecker sequence_;
};

class SourceCallBarrier {
 public:
  void Open();
  bool TryEnter();
  void Leave();
  bool Close();

 private:
  static constexpr uint32_t kClosed = uint32_t{1} << 31;
  static_assert(std::atomic<uint32_t>::is_always_lock_free);
  std::atomic<uint32_t> state_{kClosed};
};

class FakeVideoSink final : public VideoSink {
 public:
  absl::Status Start(std::shared_ptr<VideoFrameSource> frames) override;
  absl::Status SetBounds(OutputBounds bounds) override;
  absl::Status Stop() override;
  std::optional<DecodedVideoFrame> Vsync(MediaTime time);

 private:
  std::shared_ptr<VideoFrameSource> frames_;
  SourceCallBarrier source_calls_;
  bool stopped_ = false;
};

class FakeAudioSink final : public AudioSink {
 public:
  absl::Status Start(AudioInfo format,
                     std::shared_ptr<AudioSource> audio) override;
  absl::StatusOr<TimeDelta> GetOutputLatency() const override;
  absl::Status Stop() override;
  size_t Render(std::span<uint8_t> destination);

 private:
  std::shared_ptr<AudioSource> audio_;
  SourceCallBarrier source_calls_;
  AudioInfo format_;
  std::atomic<bool> invalid_read_{false};
  bool stopped_ = false;
};

struct FakeLogEntry {
  LogLevel level;
  std::string message;
};

class FakePort final : public Port {
 public:
  explicit FakePort(std::shared_ptr<ControlTaskRunner> runner,
                    FakeFaults faults = {});
  absl::StatusOr<PortLimits> GetLimits() const override;
  absl::StatusOr<std::unique_ptr<Player>> CreatePlayer(
      PlayerConfig config, std::shared_ptr<PlayerCallbacks> callbacks) override;
  absl::StatusOr<DecodingCapability> QueryDecoding(
      const CapabilityRequest& request) const override;
  absl::StatusOr<bool> CanChangeType(const StreamConfig& from,
                                     const StreamConfig& to) const override;
  absl::StatusOr<std::unique_ptr<Decoder>> CreateVideoDecoder(
      StreamConfig config, SeekTicket ticket,
      std::shared_ptr<DecoderCallbacks> callbacks) override;
  absl::StatusOr<std::unique_ptr<Decoder>> CreateAudioDecoder(
      StreamConfig config, SeekTicket ticket,
      std::shared_ptr<DecoderCallbacks> callbacks) override;
  absl::StatusOr<std::unique_ptr<VideoSink>> CreateVideoSink(
      OutputMode mode) override;
  absl::StatusOr<std::unique_ptr<AudioSink>> CreateAudioSink() override;
  absl::StatusOr<std::shared_ptr<ControlTaskRunner>> CreateTaskRunner(
      TaskPriority priority, BlockingPolicy blocking) override;
  absl::Status Log(LogLevel level, std::string_view redacted_message) override;
  absl::Status SetMemoryPressureObserver(
      std::shared_ptr<MemoryPressureObserver> observer) override;
  absl::Status SignalMemoryPressure(MemoryPressure pressure);
  std::vector<FakeLogEntry> Logs() const;

 private:
  std::shared_ptr<ControlTaskRunner> runner_;
  FakeFaults faults_;
  std::shared_ptr<InstanceBudget> budget_ = std::make_shared<InstanceBudget>();
  std::shared_ptr<MemoryPressureObserver> memory_observer_;
  mutable std::mutex hooks_mutex_;
  size_t memory_callbacks_ = 0;
  std::vector<FakeLogEntry> logs_;
};

}

#endif
