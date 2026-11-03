#include "penumbra/desktop/ffmpeg_fallback_demuxer.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <ios>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/fallback_demuxer.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/fallback_policy.h"
#include "umbra/demux/mp4_demuxer.h"
#include "umbra/settings/settings_keys.h"

extern "C" {
#include "libavutil/log.h"
}

namespace penumbra::desktop {
namespace {

using ::testing::Ge;
using ::testing::HasSubstr;
using ::testing::SizeIs;

constexpr std::chrono::seconds kWait{60};
constexpr size_t kStopAttempts = 1000000;

bool SilenceFfmpegLogs() {
  av_log_set_level(AV_LOG_QUIET);
  return true;
}

[[maybe_unused]] const bool kLogsSilenced = SilenceFfmpegLogs();

std::vector<uint8_t> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  EXPECT_TRUE(file.good()) << path;
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

std::vector<uint8_t> Fixture(std::string_view name) {
  return ReadFile(absl::StrCat(ALUMBRA_FALLBACK_FIXTURE_DIR, "/", name));
}

std::vector<uint8_t> DemuxFixture(std::string_view name) {
  return ReadFile(absl::StrCat(ALUMBRA_DEMUX_FIXTURE_DIR, "/", name));
}

class MemorySource final : public FallbackByteSource {
 public:
  explicit MemorySource(std::vector<uint8_t> bytes, bool seekable = true,
                        size_t chunk = 0)
      : bytes_(std::move(bytes)), seekable_(seekable), chunk_(chunk) {}

  absl::StatusOr<size_t> ReadAt(uint64_t offset,
                                std::span<uint8_t> destination) override {
    const auto start = static_cast<size_t>(offset);
    if (start >= bytes_.size()) {
      return size_t{0};
    }
    size_t count = std::min(destination.size(), bytes_.size() - start);
    if (chunk_ != 0) {
      count = std::min(count, chunk_);
    }
    std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(start), count,
                destination.begin());
    return count;
  }
  [[nodiscard]] std::optional<uint64_t> Size() const override {
    return bytes_.size();
  }
  [[nodiscard]] bool Seekable() const override { return seekable_; }

 private:
  std::vector<uint8_t> bytes_;
  bool seekable_;
  size_t chunk_;
};

class FailingSource final : public FallbackByteSource {
 public:
  absl::StatusOr<size_t> ReadAt(uint64_t offset,
                                std::span<uint8_t> destination) override {
    static_cast<void>(offset);
    static_cast<void>(destination);
    return absl::UnavailableError("the network went away");
  }
  [[nodiscard]] std::optional<uint64_t> Size() const override {
    return std::nullopt;
  }
  [[nodiscard]] bool Seekable() const override { return false; }
};

class BlockingSource final : public FallbackByteSource {
 public:
  absl::StatusOr<size_t> ReadAt(uint64_t offset,
                                std::span<uint8_t> destination) override {
    static_cast<void>(offset);
    static_cast<void>(destination);
    std::unique_lock lock(mutex_);
    entered_ = true;
    changed_.notify_all();
    changed_.wait(lock, [this] { return released_; });
    return size_t{0};
  }
  [[nodiscard]] std::optional<uint64_t> Size() const override {
    return std::nullopt;
  }
  [[nodiscard]] bool Seekable() const override { return false; }

  void WaitUntilEntered() {
    std::unique_lock lock(mutex_);
    ASSERT_TRUE(changed_.wait_for(lock, kWait, [this] { return entered_; }));
  }
  void Release() {
    const std::scoped_lock lock(mutex_);
    released_ = true;
    changed_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  bool entered_ = false;
  bool released_ = false;
};

struct Event {
  enum class Kind { kOpen, kSamples, kEnd, kError };
  Kind kind = Kind::kOpen;
  SeekTicket ticket = 0;
  uint32_t track_id = 0;
  std::vector<EncodedSample> samples;
  FallbackDemuxerInfo info;
  absl::Status status;
};

class Recorder final : public FallbackDemuxerCallbacks {
 public:
  absl::Status OnOpen(FallbackDemuxerInfo info) override {
    Event event;
    event.kind = Event::Kind::kOpen;
    event.info = std::move(info);
    Push(std::move(event));
    return absl::OkStatus();
  }
  absl::Status OnSamples(SeekTicket ticket, uint32_t track_id,
                         std::vector<EncodedSample> samples) override {
    Event event;
    event.kind = Event::Kind::kSamples;
    event.ticket = ticket;
    event.track_id = track_id;
    event.samples = std::move(samples);
    Push(std::move(event));
    return absl::OkStatus();
  }
  absl::Status OnEndOfStream(SeekTicket ticket, uint32_t track_id) override {
    Event event;
    event.kind = Event::Kind::kEnd;
    event.ticket = ticket;
    event.track_id = track_id;
    Push(std::move(event));
    return absl::OkStatus();
  }
  absl::Status OnError(absl::Status error) override {
    Event event;
    event.kind = Event::Kind::kError;
    event.status = std::move(error);
    Push(std::move(event));
    return absl::OkStatus();
  }

  Event Next() {
    std::unique_lock lock(mutex_);
    if (!changed_.wait_for(lock, kWait, [this] { return !events_.empty(); })) {
      Event timeout;
      timeout.kind = Event::Kind::kError;
      timeout.status = absl::DeadlineExceededError("no event");
      return timeout;
    }
    Event event = std::move(events_.front());
    events_.pop_front();
    return event;
  }

  [[nodiscard]] size_t Delivered() {
    const std::scoped_lock lock(mutex_);
    return delivered_;
  }

 private:
  void Push(Event event) {
    const std::scoped_lock lock(mutex_);
    ++delivered_;
    events_.push_back(std::move(event));
    changed_.notify_all();
  }

  std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<Event> events_;
  size_t delivered_ = 0;
};

struct Opened {
  std::unique_ptr<FallbackDemuxer> demuxer;
  std::shared_ptr<Recorder> recorder;
  FallbackDemuxerInfo info;
};

Opened Open(const std::shared_ptr<FallbackByteSource>& source,
            std::string_view name_hint = "", SeekTicket ticket = 1) {
  FfmpegFallbackProvider provider;
  auto recorder = std::make_shared<Recorder>();
  auto demuxer = provider.OpenFallbackDemuxer(
      {.initial_ticket = ticket, .name_hint = std::string(name_hint)}, source,
      recorder);
  EXPECT_TRUE(demuxer.ok()) << demuxer.status();
  if (!demuxer.ok()) {
    return {};
  }
  const Event event = recorder->Next();
  EXPECT_EQ(event.kind, Event::Kind::kOpen) << event.status;
  if (event.kind != Event::Kind::kOpen) {
    return {};
  }
  return Opened{.demuxer = *std::move(demuxer),
                .recorder = std::move(recorder),
                .info = event.info};
}

struct Drained {
  std::map<uint32_t, std::vector<EncodedSample>> samples;
  std::map<uint32_t, bool> ended;
  absl::Status error;
};

bool DrainTurn(Opened& opened, SeekTicket ticket, size_t grant, uint32_t track,
               Drained& drained) {
  const absl::Status requested =
      opened.demuxer->RequestSamples(ticket, track, grant);
  EXPECT_TRUE(requested.ok()) << requested;
  Event event = opened.recorder->Next();
  if (event.kind == Event::Kind::kError) {
    drained.error = event.status;
    return false;
  }
  EXPECT_EQ(event.track_id, track);
  EXPECT_EQ(event.ticket, ticket);
  if (event.kind == Event::Kind::kEnd) {
    drained.ended[track] = true;
    return false;
  }
  auto& bucket = drained.samples[track];
  bucket.reserve(bucket.size() + event.samples.size());
  std::ranges::move(event.samples, std::back_inserter(bucket));
  return true;
}

Drained Drain(Opened& opened, SeekTicket ticket, size_t grant) {
  Drained drained;
  std::vector<uint32_t> active;
  active.reserve(opened.info.streams.size());
  for (const StreamInfo& stream : opened.info.streams) {
    active.push_back(stream.track_id);
  }
  while (!active.empty() && drained.error.ok()) {
    std::erase_if(active, [&](uint32_t track) {
      return !DrainTurn(opened, ticket, grant, track, drained);
    });
  }
  return drained;
}

void StopAndJoin(FallbackDemuxer& demuxer) {
  for (size_t attempt = 0; attempt < kStopAttempts; ++attempt) {
    if (demuxer.Stop().ok()) {
      return;
    }
    std::this_thread::yield();
  }
  FAIL() << "the demuxer never became quiescent";
}

const StreamInfo* Find(const FallbackDemuxerInfo& info, StreamType type) {
  for (const StreamInfo& stream : info.streams) {
    if (stream.type() == type) {
      return &stream;
    }
  }
  return nullptr;
}

bool NonDecreasingDts(const std::vector<EncodedSample>& samples) {
  return std::ranges::is_sorted(samples, {}, [](const EncodedSample& sample) {
    return sample.dts.value;
  });
}

TEST(FfmpegFallbackProbeTest, RecognisesTheLongTailContainers) {
  const FfmpegFallbackProvider provider;
  struct Case {
    std::string_view file;
    std::string_view container;
  };
  for (const Case& test : {Case{.file = "h264_mp3.avi", .container = "avi"},
                           Case{.file = "opus.ogg", .container = "ogg"},
                           Case{.file = "h264_aac.flv", .container = "flv"}}) {
    const std::vector<uint8_t> bytes = Fixture(test.file);
    const auto probe =
        provider.ProbeFallbackDemuxer(std::span<const uint8_t>(bytes).first(
                                          std::min<size_t>(4096, bytes.size())),
                                      "");
    ASSERT_TRUE(probe.ok()) << probe.status();
    EXPECT_TRUE(probe->supported) << test.file;
    EXPECT_EQ(probe->container, test.container);
  }
}

TEST(FfmpegFallbackProbeTest, RecognisesAnMp4ThroughTheMovDemuxer) {
  const FfmpegFallbackProvider provider;
  const std::vector<uint8_t> bytes = Fixture("mpeg4_part2.mp4");
  const auto probe = provider.ProbeFallbackDemuxer(
      std::span<const uint8_t>(bytes).first(4096), "clip.mp4");
  ASSERT_TRUE(probe.ok());
  EXPECT_TRUE(probe->supported);
  EXPECT_THAT(probe->container, HasSubstr("mp4"));
}

TEST(FfmpegFallbackProbeTest, RejectsBytesItDoesNotKnow) {
  const FfmpegFallbackProvider provider;
  const std::vector<uint8_t> noise(4096, 0x5a);
  const auto probe = provider.ProbeFallbackDemuxer(noise, "");
  ASSERT_TRUE(probe.ok());
  EXPECT_FALSE(probe->supported);
  EXPECT_TRUE(probe->container.empty());
}

TEST(FfmpegFallbackProbeTest, AnEmptyWindowIsNotSupported) {
  const FfmpegFallbackProvider provider;
  const auto probe = provider.ProbeFallbackDemuxer({}, "");
  ASSERT_TRUE(probe.ok());
  EXPECT_FALSE(probe->supported);
}

TEST(FfmpegFallbackOpenTest, RejectsMissingArguments) {
  FfmpegFallbackProvider provider;
  auto recorder = std::make_shared<Recorder>();
  auto source = std::make_shared<MemorySource>(std::vector<uint8_t>{});
  EXPECT_EQ(provider
                .OpenFallbackDemuxer({.initial_ticket = 1, .name_hint = ""},
                                     nullptr, recorder)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(provider
                .OpenFallbackDemuxer({.initial_ticket = 1, .name_hint = ""},
                                     source, nullptr)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(provider
                .OpenFallbackDemuxer({.initial_ticket = 0, .name_hint = ""},
                                     source, recorder)
                .status()
                .code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(FfmpegFallbackOpenTest, ReportsAnAviWithItsStreams) {
  auto opened = Open(std::make_shared<MemorySource>(Fixture("h264_mp3.avi")));
  ASSERT_NE(opened.demuxer, nullptr);
  EXPECT_EQ(opened.info.container, "avi");
  EXPECT_TRUE(opened.info.seekable);
  ASSERT_THAT(opened.info.streams, SizeIs(2));
  const StreamInfo* video = Find(opened.info, StreamType::kVideo);
  const StreamInfo* audio = Find(opened.info, StreamType::kAudio);
  ASSERT_NE(video, nullptr);
  ASSERT_NE(audio, nullptr);
  EXPECT_EQ(video->codec, CodecId::kH264);
  const auto& video_format = std::get<VideoInfo>(video->format);
  EXPECT_EQ(video_format.coded_width, 96);
  EXPECT_EQ(video_format.coded_height, 64);
  EXPECT_EQ(audio->codec, CodecId::kMp3);
  const auto& audio_format = std::get<AudioInfo>(audio->format);
  EXPECT_EQ(audio_format.sample_rate, 48000);
  EXPECT_EQ(audio_format.channel_count, 2);
  EXPECT_TRUE(opened.info.duration.is_finite());
  EXPECT_GT(opened.info.duration, TimeDelta::FromMilliseconds(500));
  EXPECT_LT(opened.info.duration, TimeDelta::FromMilliseconds(1500));
  StopAndJoin(*opened.demuxer);
}

TEST(FfmpegFallbackOpenTest, DeliversEveryAviSampleWithItsTiming) {
  auto opened =
      Open(std::make_shared<MemorySource>(Fixture("h264_mp3.avi"), true, 777));
  ASSERT_NE(opened.demuxer, nullptr);
  const Drained drained = Drain(opened, 1, 4);
  ASSERT_TRUE(drained.error.ok()) << drained.error;
  const StreamInfo* video = Find(opened.info, StreamType::kVideo);
  const StreamInfo* audio = Find(opened.info, StreamType::kAudio);
  ASSERT_NE(video, nullptr);
  ASSERT_NE(audio, nullptr);
  const auto& video_samples = drained.samples.at(video->track_id);
  const auto& audio_samples = drained.samples.at(audio->track_id);
  EXPECT_THAT(video_samples, SizeIs(10));
  EXPECT_THAT(audio_samples, SizeIs(Ge(20U)));
  EXPECT_TRUE(video_samples.front().key_frame);
  EXPECT_TRUE(NonDecreasingDts(video_samples));
  EXPECT_TRUE(NonDecreasingDts(audio_samples));
  for (const EncodedSample& sample : video_samples) {
    EXPECT_FALSE(sample.data.empty());
    EXPECT_EQ(sample.track_id, video->track_id);
    EXPECT_TRUE(sample.pts.is_known());
    EXPECT_EQ(sample.pts.time_base, video->time_base);
    EXPECT_FALSE(sample.encryption.has_value());
  }
  EXPECT_TRUE(drained.ended.at(video->track_id));
  EXPECT_TRUE(drained.ended.at(audio->track_id));
  StopAndJoin(*opened.demuxer);
}

TEST(FfmpegFallbackOpenTest, ADeliveryNeverExceedsTheGrant) {
  auto opened = Open(std::make_shared<MemorySource>(Fixture("h264_mp3.avi")));
  ASSERT_NE(opened.demuxer, nullptr);
  const StreamInfo* video = Find(opened.info, StreamType::kVideo);
  ASSERT_NE(video, nullptr);
  ASSERT_TRUE(opened.demuxer->RequestSamples(1, video->track_id, 3).ok());
  const Event event = opened.recorder->Next();
  ASSERT_EQ(event.kind, Event::Kind::kSamples);
  EXPECT_THAT(event.samples,
              SizeIs(::testing::AllOf(Ge(1U), ::testing::Le(3U))));
  StopAndJoin(*opened.demuxer);
}

TEST(FfmpegFallbackOpenTest, ReportsAnOggOpusStream) {
  auto opened = Open(std::make_shared<MemorySource>(Fixture("opus.ogg")));
  ASSERT_NE(opened.demuxer, nullptr);
  EXPECT_EQ(opened.info.container, "ogg");
  ASSERT_THAT(opened.info.streams, SizeIs(1));
  const StreamInfo& stream = opened.info.streams.front();
  EXPECT_EQ(stream.codec, CodecId::kOpus);
  EXPECT_EQ(stream.type(), StreamType::kAudio);
  EXPECT_THAT(
      std::string(reinterpret_cast<const char*>(stream.codec_config.data()),
                  std::min<size_t>(8, stream.codec_config.size())),
      ::testing::Eq("OpusHead"));
  const Drained drained = Drain(opened, 1, 8);
  ASSERT_TRUE(drained.error.ok()) << drained.error;
  EXPECT_THAT(drained.samples.at(stream.track_id), SizeIs(Ge(40U)));
  EXPECT_TRUE(drained.ended.at(stream.track_id));
  StopAndJoin(*opened.demuxer);
}

TEST(FfmpegFallbackOpenTest, ReportsAnFlvWithItsCodecConfiguration) {
  auto opened = Open(std::make_shared<MemorySource>(Fixture("h264_aac.flv")));
  ASSERT_NE(opened.demuxer, nullptr);
  EXPECT_EQ(opened.info.container, "flv");
  const StreamInfo* video = Find(opened.info, StreamType::kVideo);
  const StreamInfo* audio = Find(opened.info, StreamType::kAudio);
  ASSERT_NE(video, nullptr);
  ASSERT_NE(audio, nullptr);
  EXPECT_EQ(video->codec, CodecId::kH264);
  EXPECT_FALSE(video->codec_config.empty());
  EXPECT_EQ(audio->codec, CodecId::kAac);
  EXPECT_FALSE(audio->codec_config.empty());
  const Drained drained = Drain(opened, 1, 16);
  ASSERT_TRUE(drained.error.ok()) << drained.error;
  EXPECT_THAT(drained.samples.at(video->track_id), SizeIs(10));
  StopAndJoin(*opened.demuxer);
}

TEST(FfmpegFallbackOpenTest, ReadsAnMp4ThroughTheMovDemuxer) {
  auto opened = Open(std::make_shared<MemorySource>(
      DemuxFixture("progressive_faststart.mp4")));
  ASSERT_NE(opened.demuxer, nullptr);
  EXPECT_THAT(opened.info.container, HasSubstr("mp4"));
  ASSERT_THAT(opened.info.streams, SizeIs(2));
  const Drained drained = Drain(opened, 1, 32);
  ASSERT_TRUE(drained.error.ok()) << drained.error;
  const StreamInfo* video = Find(opened.info, StreamType::kVideo);
  ASSERT_NE(video, nullptr);
  EXPECT_THAT(drained.samples.at(video->track_id), SizeIs(Ge(10U)));
  StopAndJoin(*opened.demuxer);
}

TEST(FfmpegFallbackOpenTest, ReportsACodecWithoutAnIdAsUnknown) {
  auto opened =
      Open(std::make_shared<MemorySource>(Fixture("mpeg4_part2.mp4")));
  ASSERT_NE(opened.demuxer, nullptr);
  ASSERT_THAT(opened.info.streams, SizeIs(1));
  EXPECT_EQ(opened.info.streams.front().codec, CodecId::kUnknown);
  const Drained drained = Drain(opened, 1, 16);
  ASSERT_TRUE(drained.error.ok()) << drained.error;
  EXPECT_THAT(drained.samples.at(opened.info.streams.front().track_id),
              SizeIs(10));
  StopAndJoin(*opened.demuxer);
}

TEST(FfmpegFallbackOpenTest, RejectsBytesThatAreNotAContainer) {
  FfmpegFallbackProvider provider;
  auto recorder = std::make_shared<Recorder>();
  auto demuxer = provider.OpenFallbackDemuxer(
      {.initial_ticket = 1, .name_hint = ""},
      std::make_shared<MemorySource>(std::vector<uint8_t>(8192, 0x5a)),
      recorder);
  ASSERT_TRUE(demuxer.ok());
  const Event event = recorder->Next();
  EXPECT_EQ(event.kind, Event::Kind::kError);
  EXPECT_EQ(event.status.code(), absl::StatusCode::kInvalidArgument);
  StopAndJoin(**demuxer);
  EXPECT_EQ(recorder->Delivered(), 1U);
}

TEST(FfmpegFallbackOpenTest, ASourceFailureReachesTheCallbacksUnchanged) {
  FfmpegFallbackProvider provider;
  auto recorder = std::make_shared<Recorder>();
  auto demuxer =
      provider.OpenFallbackDemuxer({.initial_ticket = 1, .name_hint = ""},
                                   std::make_shared<FailingSource>(), recorder);
  ASSERT_TRUE(demuxer.ok());
  const Event event = recorder->Next();
  EXPECT_EQ(event.kind, Event::Kind::kError);
  EXPECT_EQ(event.status.code(), absl::StatusCode::kUnavailable);
  StopAndJoin(**demuxer);
}

TEST(FfmpegFallbackOpenTest, RejectsAnEncryptedStream) {
  FfmpegFallbackProvider provider;
  auto recorder = std::make_shared<Recorder>();
  auto demuxer = provider.OpenFallbackDemuxer(
      {.initial_ticket = 1, .name_hint = ""},
      std::make_shared<MemorySource>(Fixture("encrypted_cenc.mp4")), recorder);
  ASSERT_TRUE(demuxer.ok());
  const Event event = recorder->Next();
  if (event.kind == Event::Kind::kOpen) {
    Opened opened{.demuxer = *std::move(demuxer),
                  .recorder = recorder,
                  .info = event.info};
    const Drained drained = Drain(opened, 1, 4);
    EXPECT_EQ(drained.error.code(), absl::StatusCode::kUnimplemented);
    for (const auto& [track, samples] : drained.samples) {
      EXPECT_TRUE(samples.empty());
    }
    StopAndJoin(*opened.demuxer);
    return;
  }
  EXPECT_EQ(event.kind, Event::Kind::kError);
  EXPECT_EQ(event.status.code(), absl::StatusCode::kUnimplemented);
  StopAndJoin(**demuxer);
}

TEST(FfmpegFallbackFlowTest, ChecksGrantArguments) {
  auto opened =
      Open(std::make_shared<MemorySource>(Fixture("opus.ogg")), "", 5);
  ASSERT_NE(opened.demuxer, nullptr);
  const uint32_t track = opened.info.streams.front().track_id;
  EXPECT_EQ(opened.demuxer->RequestSamples(6, track, 1).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(opened.demuxer->RequestSamples(5, track + 99, 1).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(opened.demuxer->RequestSamples(5, track, 0).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(opened.demuxer->RequestSamples(4, track, 1).ok());
  ASSERT_TRUE(opened.demuxer->RequestSamples(5, track, 1).ok());
  const Event event = opened.recorder->Next();
  EXPECT_EQ(event.kind, Event::Kind::kSamples);
  EXPECT_EQ(event.ticket, 5U);
  StopAndJoin(*opened.demuxer);
}

TEST(FfmpegFallbackFlowTest, SeekRestartsAtTheKeyFrameBeforeTheTarget) {
  auto opened = Open(std::make_shared<MemorySource>(Fixture("h264_mp3.avi")));
  ASSERT_NE(opened.demuxer, nullptr);
  const StreamInfo* video = Find(opened.info, StreamType::kVideo);
  ASSERT_NE(video, nullptr);
  ASSERT_TRUE(opened.demuxer->RequestSamples(1, video->track_id, 2).ok());
  static_cast<void>(opened.recorder->Next());
  EXPECT_EQ(opened.demuxer->Seek(1, MediaTime::FromMilliseconds(700)).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(opened.demuxer->Seek(2, MediaTime::Unknown()).code(),
            absl::StatusCode::kInvalidArgument);
  ASSERT_TRUE(opened.demuxer->Seek(2, MediaTime::FromMilliseconds(700)).ok());
  EXPECT_TRUE(opened.demuxer->RequestSamples(1, video->track_id, 1).ok());
  EXPECT_EQ(opened.demuxer->RequestSamples(3, video->track_id, 1).code(),
            absl::StatusCode::kInvalidArgument);
  ASSERT_TRUE(opened.demuxer->RequestSamples(2, video->track_id, 4).ok());
  const Event event = opened.recorder->Next();
  ASSERT_EQ(event.kind, Event::Kind::kSamples);
  EXPECT_EQ(event.ticket, 2U);
  ASSERT_FALSE(event.samples.empty());
  const EncodedSample& first = event.samples.front();
  EXPECT_TRUE(first.key_frame);
  const auto micros = Rescale(first.pts.value, first.pts.time_base,
                              kMicrosecondTimeBase, Rounding::kTowardZero);
  EXPECT_LE(micros.value_or(INT64_MAX), 700000);
  EXPECT_GE(micros.value_or(INT64_MIN), 400000);
  StopAndJoin(*opened.demuxer);
}

TEST(FfmpegFallbackFlowTest, ASeekToTheStartRedeliversTheFirstSample) {
  auto opened = Open(std::make_shared<MemorySource>(Fixture("opus.ogg")));
  ASSERT_NE(opened.demuxer, nullptr);
  const uint32_t track = opened.info.streams.front().track_id;
  const Drained first = Drain(opened, 1, 64);
  ASSERT_TRUE(first.error.ok()) << first.error;
  const size_t total = first.samples.at(track).size();
  ASSERT_TRUE(opened.demuxer->Seek(2, MediaTime::Zero()).ok());
  const Drained second = Drain(opened, 2, 64);
  ASSERT_TRUE(second.error.ok()) << second.error;
  EXPECT_EQ(second.samples.at(track).size(), total);
  StopAndJoin(*opened.demuxer);
}

TEST(FfmpegFallbackFlowTest, ASourceThatCannotSeekRejectsSeeks) {
  auto opened =
      Open(std::make_shared<MemorySource>(Fixture("opus.ogg"), false));
  ASSERT_NE(opened.demuxer, nullptr);
  EXPECT_FALSE(opened.info.seekable);
  EXPECT_EQ(opened.demuxer->Seek(2, MediaTime::Zero()).code(),
            absl::StatusCode::kUnimplemented);
  const Drained drained = Drain(opened, 1, 64);
  ASSERT_TRUE(drained.error.ok()) << drained.error;
  EXPECT_THAT(drained.samples.at(opened.info.streams.front().track_id),
              SizeIs(Ge(40U)));
  StopAndJoin(*opened.demuxer);
}

TEST(FfmpegFallbackFlowTest, StopIsTerminalAndIdempotent) {
  auto opened = Open(std::make_shared<MemorySource>(Fixture("opus.ogg")));
  ASSERT_NE(opened.demuxer, nullptr);
  const uint32_t track = opened.info.streams.front().track_id;
  StopAndJoin(*opened.demuxer);
  const size_t delivered = opened.recorder->Delivered();
  EXPECT_TRUE(opened.demuxer->Stop().ok());
  EXPECT_EQ(opened.demuxer->RequestSamples(1, track, 1).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(opened.demuxer->Seek(2, MediaTime::Zero()).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(opened.recorder->Delivered(), delivered);
}

TEST(FfmpegFallbackFlowTest, StopWaitsForABlockedSourceAndSilencesCallbacks) {
  FfmpegFallbackProvider provider;
  auto source = std::make_shared<BlockingSource>();
  auto recorder = std::make_shared<Recorder>();
  auto demuxer = provider.OpenFallbackDemuxer(
      {.initial_ticket = 1, .name_hint = ""}, source, recorder);
  ASSERT_TRUE(demuxer.ok());
  source->WaitUntilEntered();
  EXPECT_EQ((*demuxer)->Stop().code(), absl::StatusCode::kUnavailable);
  source->Release();
  StopAndJoin(**demuxer);
  EXPECT_EQ(recorder->Delivered(), 0U);
}

TEST(FfmpegFallbackFlowTest, DestroyingARunningDemuxerIsSafe) {
  auto opened = Open(std::make_shared<MemorySource>(Fixture("h264_mp3.avi")));
  ASSERT_NE(opened.demuxer, nullptr);
  ASSERT_TRUE(
      opened.demuxer->RequestSamples(1, opened.info.streams.front().track_id, 2)
          .ok());
  opened.demuxer.reset();
}

class CountingOutput final : public umbra::demux::DemuxerOutput {
 public:
  void OnStreams(std::vector<StreamInfo> streams) override {
    static_cast<void>(streams);
  }
  void OnMetadataStream(
      const umbra::demux::MetadataStreamInfo& stream) override {
    static_cast<void>(stream);
  }
  void OnSeekMap(std::shared_ptr<const umbra::demux::SeekMap> map) override {
    static_cast<void>(map);
  }
  void OnSample(EncodedSample sample) override {
    static_cast<void>(sample);
    ++samples;
  }
  void OnTimedMetadata(umbra::demux::TimedMetadataItem item) override {
    static_cast<void>(item);
  }
  void OnProducerReferenceTime(
      const umbra::demux::ProducerReferenceTime& reference) override {
    static_cast<void>(reference);
  }
  size_t samples = 0;
};

absl::Status RunOwnMp4Parser(const std::vector<uint8_t>& bytes,
                             CountingOutput& output) {
  umbra::demux::Mp4Demuxer demuxer(output);
  demuxer.SetSourceInfo(bytes.size(), true);
  absl::Status appended =
      demuxer.Append(0, SharedBytes::Copy(std::span<const uint8_t>(bytes)));
  if (!appended.ok()) {
    return appended;
  }
  demuxer.EndOfInput();
  while (true) {
    auto step = demuxer.Step();
    if (!step.ok()) {
      return step.status();
    }
    if (step->kind == umbra::demux::StepKind::kEndOfStream) {
      return absl::OkStatus();
    }
    if (step->kind == umbra::demux::StepKind::kNeedMoreData ||
        step->kind == umbra::demux::StepKind::kSeek) {
      return absl::DataLossError("the parser asked for bytes past the end");
    }
  }
}

TEST(FfmpegFallbackRetryTest, ARejectedMp4PlaysThroughTheSlotAndIsCounted) {
  const std::vector<uint8_t> bytes = Fixture("mpeg4_part2.mp4");
  CountingOutput own;
  const absl::Status rejection = RunOwnMp4Parser(bytes, own);
  ASSERT_FALSE(rejection.ok());
  EXPECT_EQ(own.samples, 0U);

  const FfmpegFallbackProvider provider;
  const auto probe = provider.ProbeFallbackDemuxer(
      std::span<const uint8_t>(bytes).first(4096), "");
  ASSERT_TRUE(probe.ok());
  umbra::demux::FallbackPolicy policy(
      umbra::settings::DemuxFfmpegMode::kFallback, true);
  const umbra::demux::ParserFacts facts = {.has_own_parser = true,
                                           .slot_accepts = probe->supported,
                                           .progressive = true};
  ASSERT_EQ(policy.ChooseFirst(facts), umbra::demux::ParserChoice::kOwn);
  ASSERT_EQ(
      policy.ChooseAfterOwnRejection(rejection, own.samples != 0, "mp4", facts),
      umbra::demux::ParserChoice::kFallback);
  EXPECT_EQ(policy.Stats().retries, 1U);

  auto opened = Open(std::make_shared<MemorySource>(bytes));
  ASSERT_NE(opened.demuxer, nullptr);
  const Drained drained = Drain(opened, 1, 16);
  ASSERT_TRUE(drained.error.ok()) << drained.error;
  EXPECT_THAT(drained.samples.at(opened.info.streams.front().track_id),
              SizeIs(10));
  StopAndJoin(*opened.demuxer);
}

TEST(FfmpegFallbackRetryTest, AFileTheOwnParserAcceptsIsNotRetried) {
  const std::vector<uint8_t> bytes = DemuxFixture("progressive_faststart.mp4");
  CountingOutput own;
  const absl::Status status = RunOwnMp4Parser(bytes, own);
  EXPECT_TRUE(status.ok()) << status;
  EXPECT_GT(own.samples, 0U);
  umbra::demux::FallbackPolicy policy(
      umbra::settings::DemuxFfmpegMode::kFallback, true);
  EXPECT_EQ(
      policy.ChooseAfterOwnRejection(
          status, true, "mp4",
          {.has_own_parser = true, .slot_accepts = true, .progressive = true}),
      umbra::demux::ParserChoice::kUnsupported);
  EXPECT_EQ(policy.Stats().retries, 0U);
}

}
}
