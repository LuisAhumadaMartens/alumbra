#include "penumbra/desktop/audio/audio_sink.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "audio_test_support.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop {
namespace {

using testing::RecordingLog;
using testing::RecordingObserver;
using testing::ScriptedSource;
using testing::WaitUntil;

constexpr int32_t kRate = 48000;
constexpr int32_t kChannels = 2;
constexpr int32_t kBufferFrames = 480;
constexpr size_t kChannelCount = 2;
constexpr size_t kSkipFrames = 2000;
constexpr size_t kPlateauOffset = 400;
constexpr size_t kPlateauFrames = 1000;

AudioInfo StereoFloat() {
  AudioInfo info;
  info.sample_rate = kRate;
  info.channel_count = kChannels;
  info.sample_format = SampleFormat::kF32;
  return info;
}

std::vector<float> ReadCapture(const std::string& path) {
  std::vector<float> samples;
  std::ifstream file(path, std::ios::binary);
  float value = 0.0F;
  while (file.read(reinterpret_cast<char*>(&value), sizeof(value))) {
    samples.push_back(value);
  }
  return samples;
}

class DesktopAudioSinkTest : public ::testing::Test {
 protected:
  void SetUp() override {
    capture_path_ =
        ::testing::TempDir() + "alumbra_audio_capture_" +
        ::testing::UnitTest::GetInstance()->current_test_info()->name() +
        ".raw";
    SDL_SetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, capture_path_.c_str());
    SDL_SetHint(SDL_HINT_AUDIO_DISK_TIMESCALE, "0.2");
    source_ = std::make_shared<ScriptedSource>(kChannels, 4);
    log_ = std::make_shared<RecordingLog>();
    observer_ = std::make_shared<RecordingObserver>();
  }

  void TearDown() override {
    sink_.reset();
    std::error_code ignored;
    std::filesystem::remove(capture_path_, ignored);
    SDL_ResetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE);
    SDL_ResetHint(SDL_HINT_AUDIO_DISK_TIMESCALE);
    SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
  }

  [[nodiscard]] AudioSinkOptions Options(
      const std::string& driver = "disk") const {
    AudioSinkOptions options;
    options.driver = driver;
    options.buffer_frames = kBufferFrames;
    options.log = log_;
    options.observer = observer_;
    return options;
  }

  void StartSink(AudioSinkOptions options) {
    sink_ = std::make_unique<DesktopAudioSink>(std::move(options));
    ASSERT_TRUE(sink_->Start(StereoFloat(), source_).ok());
  }

  std::string capture_path_;
  std::shared_ptr<ScriptedSource> source_;
  std::shared_ptr<RecordingLog> log_;
  std::shared_ptr<RecordingObserver> observer_;
  std::unique_ptr<DesktopAudioSink> sink_;
};

TEST_F(DesktopAudioSinkTest, StartValidatesItsArguments) {
  DesktopAudioSink sink(Options());
  EXPECT_EQ(sink.Start(StereoFloat(), nullptr).code(),
            absl::StatusCode::kInvalidArgument);
  AudioInfo bad = StereoFloat();
  bad.sample_rate = 0;
  EXPECT_EQ(sink.Start(bad, source_).code(),
            absl::StatusCode::kInvalidArgument);
  bad = StereoFloat();
  bad.channel_count = 0;
  EXPECT_EQ(sink.Start(bad, source_).code(),
            absl::StatusCode::kInvalidArgument);
  bad = StereoFloat();
  bad.sample_format = SampleFormat::kU8;
  EXPECT_EQ(sink.Start(bad, source_).code(), absl::StatusCode::kUnimplemented);
  bad.sample_format = SampleFormat::kF32Planar;
  EXPECT_EQ(sink.Start(bad, source_).code(), absl::StatusCode::kUnimplemented);
  bad = StereoFloat();
  bad.channel_count = 12;
  EXPECT_EQ(sink.Start(bad, source_).code(), absl::StatusCode::kUnimplemented);
  EXPECT_EQ(sink.Pause().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(sink.GetPosition().status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_TRUE(sink.GetOutputLatency()->is_unknown());
}

TEST_F(DesktopAudioSinkTest, StartTwiceOrAfterStopIsAPreconditionFailure) {
  StartSink(Options());
  EXPECT_EQ(sink_->Start(StereoFloat(), source_).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_TRUE(sink_->Stop().ok());
  EXPECT_TRUE(sink_->Stop().ok());
  EXPECT_EQ(sink_->Start(StereoFloat(), source_).code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST_F(DesktopAudioSinkTest, UnknownDeviceNameFailsToStart) {
  AudioSinkOptions options = Options();
  options.device_name = "no such device";
  DesktopAudioSink sink(std::move(options));
  EXPECT_EQ(sink.Start(StereoFloat(), source_).code(),
            absl::StatusCode::kNotFound);
}

TEST_F(DesktopAudioSinkTest, ReportsAndLogsOutputLatencyAtOpen) {
  StartSink(Options());
  const absl::StatusOr<TimeDelta> latency = sink_->GetOutputLatency();
  ASSERT_TRUE(latency.ok()) << latency.status();
  ASSERT_TRUE(latency->is_finite());
  EXPECT_GE(latency->InMicroseconds(), kBufferFrames * 1'000'000LL / kRate);
  const absl::StatusOr<AudioRoute> route = sink_->GetRoute();
  ASSERT_TRUE(route.ok()) << route.status();
  EXPECT_EQ(route->driver, "disk");
  EXPECT_EQ(route->buffer_frames, kBufferFrames);
  EXPECT_TRUE(log_->Contains("Audio output opened"));
  EXPECT_TRUE(log_->Contains("output latency"));
}

TEST_F(DesktopAudioSinkTest, CapturesTheSourceContiguouslyAndTracksPosition) {
  source_->SetMode(ScriptedSource::Mode::kCounter);
  StartSink(Options());
  int64_t previous_played = 0;
  for (int round = 0; round < 40; ++round) {
    const int64_t target = source_->FramesRead() + kBufferFrames;
    ASSERT_TRUE(WaitUntil([&] { return source_->FramesRead() >= target; }));
    const absl::StatusOr<AudioPosition> position = sink_->GetPosition();
    ASSERT_TRUE(position.ok()) << position.status();
    EXPECT_GE(position->media_frames_played, previous_played);
    EXPECT_LE(position->media_frames_played, position->media_frames_delivered);
    EXPECT_LE(position->media_frames_delivered, source_->FramesRead());
    previous_played = position->media_frames_played;
  }
  const absl::StatusOr<AudioPosition> last = sink_->GetPosition();
  ASSERT_TRUE(last.ok());
  ASSERT_TRUE(sink_->Stop().ok());

  const std::vector<float> capture = ReadCapture(capture_path_);
  const int64_t consumed_frames =
      static_cast<int64_t>(capture.size()) / kChannels;
  EXPECT_LE(last->media_frames_played, consumed_frames + kBufferFrames);
  EXPECT_GE(consumed_frames,
            source_->FramesRead() - (4 * static_cast<int64_t>(kBufferFrames)));
  size_t first = 0;
  while (first < capture.size() && capture[first] == 0.0F) {
    ++first;
  }
  first += kSkipFrames * kChannelCount;
  ASSERT_LT(first + 2, capture.size());
  size_t checked = 0;
  for (size_t index = first; index + kChannelCount < capture.size();
       index += kChannelCount) {
    ASSERT_EQ(capture[index], capture[index + 1]);
    ASSERT_EQ(capture[index + kChannelCount] - capture[index], 1.0F)
        << "at frame " << index / kChannelCount;
    ++checked;
  }
  EXPECT_GT(checked, 1000U);
}

TEST_F(DesktopAudioSinkTest, PauseSilencesAndResumeContinuesFromTheSameFrame) {
  source_->SetMode(ScriptedSource::Mode::kCounter);
  StartSink(Options());
  ASSERT_TRUE(WaitUntil([&] { return source_->FramesRead() > 5000; }));
  ASSERT_TRUE(sink_->Pause().ok());
  int64_t frozen = -1;
  ASSERT_TRUE(WaitUntil([&] {
    const int64_t now = source_->FramesRead();
    const bool stable = now == frozen;
    frozen = now;
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    return stable;
  }));
  const int64_t paused_at = source_->FramesRead();
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  ASSERT_EQ(source_->FramesRead(), paused_at);
  ASSERT_TRUE(sink_->Resume().ok());
  ASSERT_TRUE(
      WaitUntil([&] { return source_->FramesRead() > paused_at + 5000; }));
  ASSERT_TRUE(sink_->Stop().ok());

  const std::vector<float> capture = ReadCapture(capture_path_);
  ASSERT_GT(capture.size(), 20000U);
  size_t last_silence = 0;
  bool saw_audio = false;
  bool saw_silence_after_audio = false;
  for (size_t index = kSkipFrames * kChannelCount; index < capture.size();
       index += kChannelCount) {
    if (capture[index] != 0.0F) {
      saw_audio = true;
    } else if (saw_audio) {
      saw_silence_after_audio = true;
      last_silence = index;
    }
  }
  EXPECT_TRUE(saw_silence_after_audio);
  const size_t plateau = last_silence + (kChannelCount * kPlateauOffset);
  ASSERT_LT(plateau + (kChannelCount * kPlateauFrames), capture.size());
  EXPECT_GE(capture[plateau], static_cast<float>(paused_at));
  EXPECT_LE(capture[plateau], static_cast<float>(paused_at + 1500));
  for (size_t index = plateau;
       index < plateau + (kChannelCount * kPlateauFrames);
       index += kChannelCount) {
    ASSERT_EQ(capture[index + kChannelCount] - capture[index], 1.0F);
  }
}

TEST_F(DesktopAudioSinkTest, FlushDiscardsPreSeekAudio) {
  source_->SetConstant(0.9F);
  StartSink(Options());
  ASSERT_TRUE(WaitUntil([&] { return source_->FramesRead() > 5000; }));
  source_->SetMode(ScriptedSource::Mode::kSilentGate);
  ASSERT_TRUE(sink_->Flush().ok());
  const absl::StatusOr<AudioPosition> flushed = sink_->GetPosition();
  ASSERT_TRUE(flushed.ok());
  EXPECT_EQ(flushed->media_frames_delivered, 0);
  EXPECT_EQ(flushed->media_frames_played, 0);
  EXPECT_EQ(flushed->flush_generation, 1U);
  source_->SetConstant(0.5F);
  source_->SetMode(ScriptedSource::Mode::kConstant);
  const int64_t resumed_at = source_->FramesRead();
  ASSERT_TRUE(
      WaitUntil([&] { return source_->FramesRead() > resumed_at + 5000; }));
  ASSERT_TRUE(sink_->Stop().ok());

  const std::vector<float> capture = ReadCapture(capture_path_);
  size_t first_post_seek = capture.size();
  for (size_t index = 0; index < capture.size(); ++index) {
    if (capture[index] == 0.5F) {
      first_post_seek = index;
      break;
    }
  }
  ASSERT_LT(first_post_seek, capture.size());
  for (size_t index = first_post_seek; index < capture.size(); ++index) {
    ASSERT_LE(capture[index], 0.5F) << "pre-seek sample at " << index;
  }
  bool had_pre_seek_audio = false;
  for (size_t index = 0; index < first_post_seek; ++index) {
    had_pre_seek_audio = had_pre_seek_audio || capture[index] > 0.6F;
  }
  EXPECT_TRUE(had_pre_seek_audio);
}

TEST_F(DesktopAudioSinkTest, VolumeMuteAndArgumentChecks) {
  StartSink(Options());
  EXPECT_EQ(sink_->SetVolume(-0.1F).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(sink_->SetVolume(1.5F).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(sink_->SetVolume(0.5F).ok());
  EXPECT_TRUE(sink_->SetMuted(true).ok());
  ASSERT_TRUE(WaitUntil([&] { return source_->FramesRead() > 20000; }));
  EXPECT_TRUE(sink_->SetMuted(false).ok());
  ASSERT_TRUE(sink_->Stop().ok());
  const std::vector<float> capture = ReadCapture(capture_path_);
  ASSERT_GT(capture.size(), 2000U);
  float peak = 0.0F;
  for (size_t index = 4000; index < 4000 + 8000 && index < capture.size();
       ++index) {
    peak = std::max(peak, capture[index]);
  }
  EXPECT_LT(peak, 0.01F);
}

TEST_F(DesktopAudioSinkTest, DummyDriverRunsWithoutADevice) {
  StartSink(Options("dummy"));
  ASSERT_TRUE(WaitUntil([&] { return source_->FramesRead() > 10000; }));
  const absl::StatusOr<AudioPosition> position = sink_->GetPosition();
  ASSERT_TRUE(position.ok()) << position.status();
  EXPECT_GT(position->media_frames_delivered, 0);
  EXPECT_EQ(sink_->GetRoute()->driver, "dummy");
  EXPECT_TRUE(sink_->Stop().ok());
}

std::vector<SDL_AudioDeviceID> PlaybackDevices() {
  int count = 0;
  SDL_AudioDeviceID* devices = SDL_GetAudioPlaybackDevices(&count);
  std::vector<SDL_AudioDeviceID> result;
  if (devices != nullptr) {
    const std::span<const SDL_AudioDeviceID> listed(devices,
                                                    static_cast<size_t>(count));
    result.assign(listed.begin(), listed.end());
  }
  SDL_free(devices);
  return result;
}

SDL_AudioDeviceID FindDevice(const std::string& name) {
  SDL_AudioDeviceID found = 0;
  for (const SDL_AudioDeviceID device : PlaybackDevices()) {
    const char* candidate = SDL_GetAudioDeviceName(device);
    if (candidate != nullptr && name == candidate) {
      found = device;
    }
  }
  return found;
}

std::string FirstDeviceName() {
  const std::vector<SDL_AudioDeviceID> devices = PlaybackDevices();
  if (devices.empty()) {
    return "";
  }
  const char* candidate = SDL_GetAudioDeviceName(devices.front());
  return candidate != nullptr ? candidate : "";
}

void PushDeviceEvent(SDL_EventType type, SDL_AudioDeviceID device) {
  SDL_Event event;
  SDL_zero(event);
  event.type = type;
  event.adevice.type = type;
  event.adevice.which = device;
  event.adevice.recording = false;
  ASSERT_TRUE(SDL_PushEvent(&event));
}

TEST_F(DesktopAudioSinkTest, RemovedDeviceIsReopenedOnTheDefaultDevice) {
  SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "disk");
  ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_AUDIO));
  const std::string name = FirstDeviceName();
  ASSERT_FALSE(name.empty());
  SDL_QuitSubSystem(SDL_INIT_AUDIO);

  AudioSinkOptions options = Options();
  options.device_name = name;
  source_->SetMode(ScriptedSource::Mode::kCounter);
  StartSink(std::move(options));
  const SDL_AudioDeviceID id = FindDevice(name);
  ASSERT_NE(id, 0U);
  ASSERT_TRUE(WaitUntil([&] { return source_->FramesRead() > 5000; }));
  PushDeviceEvent(SDL_EVENT_AUDIO_DEVICE_REMOVED, id);
  ASSERT_TRUE(sink_->ProcessDeviceEvents().ok());
  ASSERT_EQ(observer_->changes.size(), 1U);
  EXPECT_EQ(observer_->changes[0].first, AudioRouteChange::kDeviceChanged);
  EXPECT_TRUE(observer_->changes[0].second.output_latency.is_finite());
  EXPECT_TRUE(observer_->errors.empty());
  EXPECT_TRUE(log_->Contains("device removed"));
  const int64_t reopened_at = source_->FramesRead();
  ASSERT_TRUE(
      WaitUntil([&] { return source_->FramesRead() > reopened_at + 3000; }));
  EXPECT_TRUE(sink_->GetPosition().ok());
  EXPECT_TRUE(sink_->ProcessDeviceEvents().ok());
  EXPECT_TRUE(sink_->Stop().ok());
}

TEST_F(DesktopAudioSinkTest,
       RemovalWithoutAReplacementReportsCapabilityChanged) {
  SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "disk");
  ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_AUDIO));
  const std::string name = FirstDeviceName();
  SDL_QuitSubSystem(SDL_INIT_AUDIO);

  AudioSinkOptions options = Options();
  options.device_name = name;
  StartSink(std::move(options));
  const SDL_AudioDeviceID id = FindDevice(name);
  ASSERT_NE(id, 0U);
  ASSERT_TRUE(WaitUntil([&] { return source_->FramesRead() > 2000; }));
  const std::string unwritable =
      ::testing::TempDir() + "no_such_directory/x.raw";
  SDL_SetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, unwritable.c_str());
  PushDeviceEvent(SDL_EVENT_AUDIO_DEVICE_REMOVED, id);
  const absl::Status status = sink_->ProcessDeviceEvents();
  EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
  EXPECT_NE(std::string(status.message()).find("capability changed"),
            std::string::npos);
  ASSERT_EQ(observer_->errors.size(), 1U);
  EXPECT_EQ(observer_->errors[0].category, PortErrorCategory::kCapability);
  EXPECT_EQ(observer_->errors[0].recovery, RecoveryHint::kRecreate);
  EXPECT_TRUE(sink_->GetOutputLatency()->is_unknown());
  EXPECT_TRUE(sink_->Pause().ok());
  EXPECT_TRUE(sink_->Flush().ok());
  EXPECT_TRUE(sink_->GetPosition().ok());
  EXPECT_TRUE(sink_->Stop().ok());
}

TEST_F(DesktopAudioSinkTest, FormatChangeReportsTheNewLatency) {
  SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "disk");
  ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_AUDIO));
  const std::string name = FirstDeviceName();
  SDL_QuitSubSystem(SDL_INIT_AUDIO);

  AudioSinkOptions options = Options();
  options.device_name = name;
  StartSink(std::move(options));
  const SDL_AudioDeviceID id = FindDevice(name);
  ASSERT_NE(id, 0U);
  PushDeviceEvent(SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED, id);
  EXPECT_TRUE(sink_->ProcessDeviceEvents().ok());
  ASSERT_EQ(observer_->changes.size(), 1U);
  EXPECT_EQ(observer_->changes[0].first, AudioRouteChange::kFormatChanged);
  EXPECT_TRUE(observer_->changes[0].second.output_latency.is_finite());
  EXPECT_TRUE(log_->Contains("format changed"));
  EXPECT_TRUE(sink_->ProcessDeviceEvents().ok());
  EXPECT_EQ(observer_->changes.size(), 1U);
  EXPECT_TRUE(sink_->Stop().ok());
}

}
}
