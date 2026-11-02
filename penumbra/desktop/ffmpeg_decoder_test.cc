#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "penumbra/desktop/decode_test_util.h"
#include "penumbra/desktop/decoder_probe.h"
#include "penumbra/desktop/desktop_component_factory.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/codec/flac.h"

extern "C" {
#include "libavutil/log.h"
}

namespace penumbra::desktop {
namespace {

using decode_test::ClipStream;
using decode_test::DecodeOutput;
using decode_test::DecodeRequest;
using decode_test::EventCollector;
using ::testing::IsEmpty;

class QuietLog : public ::testing::Environment {
 public:
  void SetUp() override { av_log_set_level(AV_LOG_QUIET); }
};

const auto* const kQuietLog = ::testing::AddGlobalTestEnvironment(new QuietLog);

struct VideoFixture {
  std::string_view file;
  std::string_view decoder;
  PixelFormat format;
  bool has_framemd5;
};

constexpr VideoFixture kVideoFixtures[] = {
    {"h264_high_bframes.mp4", "ffmpeg.h264", PixelFormat::kI420, true},
    {"h264_baseline.h264", "ffmpeg.h264", PixelFormat::kI420, true},
    {"hevc_main_bframes.mp4", "ffmpeg.hevc", PixelFormat::kI420, true},
    {"hevc_main10_bframes.mp4", "ffmpeg.hevc", PixelFormat::kI010, true},
    {"vp9_profile0.webm", "ffmpeg.vp9", PixelFormat::kI420, true},
    {"vp9_profile2.webm", "ffmpeg.vp9", PixelFormat::kI010, true},
};

struct AudioFixture {
  std::string_view file;
  std::string_view decoder;
};

constexpr AudioFixture kAudioFixtures[] = {
    {"aac_lc.m4a", "ffmpeg.aac"},   {"opus.ogg", "ffmpeg.opus"},
    {"mp3.mp3", "ffmpeg.mp3float"}, {"ac3.ac3", "ffmpeg.ac3"},
    {"eac3.eac3", "ffmpeg.eac3"},   {"flac.flac", "ffmpeg.flac"},
};

StreamConfig ConfigOf(const ClipStream& stream) {
  return {.info = stream.info,
          .encryption_scheme = std::nullopt,
          .output_mode = OutputMode::kTexture};
}

class Session {
 public:
  Session(std::string_view decoder_name, const ClipStream& stream,
          int32_t threads = 0, SeekTicket ticket = 1)
      : factory_({.video_threads = threads}),
        events_(std::make_shared<EventCollector>()) {
    const StreamType type = stream.info.type();
    auto created = factory_.CreateDecoder(
        decode_test::FindCandidate(factory_, type, decoder_name),
        ConfigOf(stream), ticket, events_);
    status_ = created.status();
    if (created.ok()) {
      decoder_ = *std::move(created);
    }
  }

  ~Session() {
    if (decoder_ != nullptr) {
      EXPECT_TRUE(decoder_->Stop().ok());
    }
  }

  DecodeOutput Run(std::span<const EncodedSample> samples,
                   SeekTicket ticket = 1, bool end_stream = true,
                   size_t stop_after_writes = 0) {
    return decode_test::RunDecode({.decoder = decoder_.get(),
                                   .events = events_.get(),
                                   .ticket = ticket,
                                   .samples = samples,
                                   .end_stream = end_stream,
                                   .stop_after_writes = stop_after_writes});
  }

  const absl::Status& status() const { return status_; }
  Decoder& decoder() { return *decoder_; }
  EventCollector& events() { return *events_; }

 private:
  DesktopComponentFactory factory_;
  std::shared_ptr<EventCollector> events_;
  std::unique_ptr<Decoder> decoder_;
  absl::Status status_;
};

ClipStream Load(std::string_view file, StreamType type) {
  auto stream = decode_test::ReadFirstStream(file, type);
  EXPECT_TRUE(stream.ok()) << stream.status();
  return stream.ok() ? *std::move(stream) : ClipStream{};
}

std::vector<std::string> Digests(const std::vector<DecodedVideoFrame>& frames) {
  std::vector<std::string> digests;
  for (const auto& frame : frames) {
    digests.push_back(decode_test::DigestVideoFrame(frame));
  }
  return digests;
}

std::vector<std::string> Digests(const std::vector<DecodedAudioBuffer>& audio) {
  std::vector<std::string> digests;
  for (const auto& buffer : audio) {
    digests.push_back(decode_test::DigestAudioBuffer(buffer));
  }
  return digests;
}

int64_t TotalSamples(const std::vector<DecodedAudioBuffer>& audio) {
  int64_t total = 0;
  for (const auto& buffer : audio) {
    total += buffer.frame_count;
  }
  return total;
}

TEST(FactoryTest, ListsTheBundledSoftwareCandidatesForEachStreamType) {
  DesktopComponentFactory factory;
  const auto video = factory.GetDecoderCandidates(StreamType::kVideo);
  ASSERT_TRUE(video.ok());
  std::vector<std::string> video_names;
  for (const DecoderCandidate& candidate : *video) {
    video_names.push_back(candidate.name);
    EXPECT_EQ(candidate.kind, DecoderKind::kBundledSoftware);
    EXPECT_EQ(candidate.output, DecoderOutput::kCpuBuffer);
    EXPECT_FALSE(candidate.secure);
    EXPECT_EQ(candidate.codecs.size(), 1U);
    EXPECT_EQ(candidate.max_width, 0);
  }
  EXPECT_THAT(video_names, ::testing::IsSupersetOf(
                               {"ffmpeg.h264", "ffmpeg.hevc", "ffmpeg.vp9"}));
  const auto audio = factory.GetDecoderCandidates(StreamType::kAudio);
  ASSERT_TRUE(audio.ok());
  std::vector<std::string> audio_names;
  for (const DecoderCandidate& candidate : *audio) {
    audio_names.push_back(candidate.name);
  }
  EXPECT_THAT(audio_names, ::testing::UnorderedElementsAre(
                               "ffmpeg.aac", "ffmpeg.opus", "ffmpeg.mp3float",
                               "ffmpeg.ac3", "ffmpeg.eac3", "ffmpeg.flac"));
  const auto text = factory.GetDecoderCandidates(StreamType::kText);
  ASSERT_TRUE(text.ok());
  EXPECT_THAT(*text, IsEmpty());
}

class MissingDecoderProbe final : public DecoderProbe {
 public:
  [[nodiscard]] absl::StatusOr<bool> IsCompiledIn(
      std::string_view decoder_name) const override {
    return decoder_name != "hevc";
  }
};

TEST(FactoryTest, OmitsDecodersThatAreNotCompiledIn) {
  DesktopComponentFactory factory({}, std::make_shared<MissingDecoderProbe>());
  const auto video = factory.GetDecoderCandidates(StreamType::kVideo);
  ASSERT_TRUE(video.ok());
  for (const DecoderCandidate& candidate : *video) {
    EXPECT_NE(candidate.name, "ffmpeg.hevc");
  }
}

TEST(FactoryTest, RejectsUnknownCandidatesAndMismatchedCodecs) {
  const ClipStream stream = Load("h264_high_bframes.mp4", StreamType::kVideo);
  DesktopComponentFactory factory;
  auto events = std::make_shared<EventCollector>();
  DecoderCandidate unknown;
  unknown.name = "ffmpeg.unknown";
  EXPECT_EQ(factory.CreateDecoder(unknown, ConfigOf(stream), 1, events)
                .status()
                .code(),
            absl::StatusCode::kNotFound);
  const DecoderCandidate hevc =
      decode_test::FindCandidate(factory, StreamType::kVideo, "ffmpeg.hevc");
  EXPECT_EQ(
      factory.CreateDecoder(hevc, ConfigOf(stream), 1, events).status().code(),
      absl::StatusCode::kInvalidArgument);
  const DecoderCandidate h264 =
      decode_test::FindCandidate(factory, StreamType::kVideo, "ffmpeg.h264");
  EXPECT_EQ(
      factory.CreateDecoder(h264, ConfigOf(stream), 1, nullptr).status().code(),
      absl::StatusCode::kInvalidArgument);
  StreamConfig protected_stream = ConfigOf(stream);
  protected_stream.encryption_scheme = EncryptionScheme::kCenc;
  EXPECT_EQ(
      factory.CreateDecoder(h264, protected_stream, 1, events).status().code(),
      absl::StatusCode::kFailedPrecondition);
}

TEST(FactoryTest, ValidatesTheThreadCount) {
  const ClipStream stream = Load("h264_high_bframes.mp4", StreamType::kVideo);
  for (const int32_t threads : {-1, 65}) {
    Session session("ffmpeg.h264", stream, threads);
    EXPECT_EQ(session.status().code(), absl::StatusCode::kInvalidArgument)
        << threads;
  }
  for (const int32_t threads : {0, 1, 64}) {
    Session session("ffmpeg.h264", stream, threads);
    EXPECT_TRUE(session.status().ok()) << threads << session.status();
  }
}

TEST(FactoryTest, HasNoSinksYet) {
  DesktopComponentFactory factory;
  EXPECT_EQ(factory.CreateVideoSink(OutputMode::kTexture).status().code(),
            absl::StatusCode::kUnimplemented);
  EXPECT_EQ(factory.CreateAudioSink().status().code(),
            absl::StatusCode::kUnimplemented);
}

class VideoDecodeTest : public ::testing::TestWithParam<VideoFixture> {};

TEST_P(VideoDecodeTest, MatchesTheFramemd5ReferenceForAnyThreadCount) {
  const VideoFixture fixture = GetParam();
  const ClipStream stream = Load(fixture.file, StreamType::kVideo);
  const std::vector<std::string> expected =
      decode_test::ReadFrameMd5(std::string(fixture.file) + ".framemd5");
  ASSERT_FALSE(expected.empty());
  for (const int32_t threads : {1, 0, 4}) {
    Session session(fixture.decoder, stream, threads);
    ASSERT_TRUE(session.status().ok()) << session.status();
    const DecodeOutput output = session.Run(stream.samples);
    EXPECT_FALSE(output.timed_out) << threads;
    EXPECT_TRUE(output.end_of_stream) << threads;
    EXPECT_THAT(output.errors, IsEmpty()) << threads;
    EXPECT_EQ(Digests(output.video), expected) << "threads " << threads;
    for (const DecodedVideoFrame& frame : output.video) {
      EXPECT_EQ(frame.format, fixture.format);
      EXPECT_EQ(frame.width, 160);
      EXPECT_EQ(frame.height, 96);
    }
  }
}

TEST_P(VideoDecodeTest, ReordersBFramesIntoPresentationOrder) {
  const VideoFixture fixture = GetParam();
  const ClipStream stream = Load(fixture.file, StreamType::kVideo);
  Session session(fixture.decoder, stream);
  ASSERT_TRUE(session.status().ok());
  const DecodeOutput output = session.Run(stream.samples);
  std::vector<int64_t> expected;
  for (const EncodedSample& sample : stream.samples) {
    expected.push_back(
        ToMediaTime(sample.pts, Rounding::kNearest).value().InMicroseconds());
  }
  std::sort(expected.begin(), expected.end());
  std::vector<int64_t> actual;
  for (const DecodedVideoFrame& frame : output.video) {
    actual.push_back(frame.presentation_time.InMicroseconds());
    EXPECT_GT(frame.duration.InMicroseconds(), 0);
  }
  EXPECT_EQ(actual, expected);
}

TEST_P(VideoDecodeTest, FlushDeliversNoStaleFramesAndRestartsAtAKeyFrame) {
  const VideoFixture fixture = GetParam();
  const ClipStream stream = Load(fixture.file, StreamType::kVideo);
  size_t second_key = 0;
  size_t keys = 0;
  for (size_t index = 0; index < stream.samples.size(); ++index) {
    if (stream.samples[index].key_frame && ++keys == 2) {
      second_key = index;
    }
  }
  ASSERT_GT(second_key, 0U);
  Session session(fixture.decoder, stream);
  ASSERT_TRUE(session.status().ok());
  const DecodeOutput before = session.Run(stream.samples, 1, false, 1);
  EXPECT_FALSE(before.timed_out);
  for (const SeekTicket ticket : before.frame_tickets) {
    EXPECT_EQ(ticket, 1U);
  }
  ASSERT_TRUE(session.decoder().Flush(2).ok());
  const std::span<const EncodedSample> tail =
      std::span<const EncodedSample>(stream.samples).subspan(second_key);
  const DecodeOutput after = session.Run(tail, 2);
  EXPECT_TRUE(after.end_of_stream);
  EXPECT_THAT(after.errors, IsEmpty());
  for (const SeekTicket ticket : after.frame_tickets) {
    EXPECT_EQ(ticket, 2U);
  }
  ClipStream tail_stream;
  tail_stream.info = stream.info;
  tail_stream.samples.assign(tail.begin(), tail.end());
  auto reference = decode_test::ReferenceDecode(tail_stream);
  ASSERT_TRUE(reference.ok());
  EXPECT_EQ(Digests(after.video), Digests(reference->video));
  EXPECT_FALSE(after.video.empty());
}

INSTANTIATE_TEST_SUITE_P(Fixtures, VideoDecodeTest,
                         ::testing::ValuesIn(kVideoFixtures),
                         [](const auto& info) {
                           std::string name(info.param.file);
                           std::replace(name.begin(), name.end(), '.', '_');
                           return name;
                         });

class AudioDecodeTest : public ::testing::TestWithParam<AudioFixture> {};

TEST_P(AudioDecodeTest, MatchesTheReferenceDecode) {
  const AudioFixture fixture = GetParam();
  const ClipStream stream = Load(fixture.file, StreamType::kAudio);
  const auto reference = decode_test::ReferenceDecode(stream);
  ASSERT_TRUE(reference.ok()) << reference.status();
  Session session(fixture.decoder, stream);
  ASSERT_TRUE(session.status().ok()) << session.status();
  const DecodeOutput output = session.Run(stream.samples);
  EXPECT_FALSE(output.timed_out);
  EXPECT_TRUE(output.end_of_stream);
  EXPECT_THAT(output.errors, IsEmpty());
  EXPECT_EQ(Digests(output.audio), Digests(reference->audio));
  ASSERT_FALSE(output.audio.empty());
  int64_t previous = INT64_MIN;
  for (const DecodedAudioBuffer& buffer : output.audio) {
    EXPECT_EQ(buffer.sample_rate, 48000);
    EXPECT_EQ(buffer.channel_count, 2);
    EXPECT_NE(buffer.presentation_time, MediaTime::Unknown());
    EXPECT_GT(buffer.presentation_time.InMicroseconds(), previous);
    previous = buffer.presentation_time.InMicroseconds();
  }
  EXPECT_GE(TotalSamples(output.audio), 44000);
  EXPECT_LE(TotalSamples(output.audio), 50000);
}

TEST_P(AudioDecodeTest, FlushRestartsDecodingWithANewTicket) {
  const AudioFixture fixture = GetParam();
  const ClipStream stream = Load(fixture.file, StreamType::kAudio);
  Session session(fixture.decoder, stream);
  ASSERT_TRUE(session.status().ok());
  const DecodeOutput first = session.Run(stream.samples, 1, false, 1);
  EXPECT_FALSE(first.timed_out);
  ASSERT_TRUE(session.decoder().Flush(2).ok());
  const DecodeOutput second = session.Run(stream.samples, 2);
  EXPECT_TRUE(second.end_of_stream);
  for (const SeekTicket ticket : second.frame_tickets) {
    EXPECT_EQ(ticket, 2U);
  }
  const auto reference = decode_test::ReferenceDecode(stream);
  ASSERT_TRUE(reference.ok());
  EXPECT_EQ(TotalSamples(second.audio), TotalSamples(reference->audio));
}

INSTANTIATE_TEST_SUITE_P(Fixtures, AudioDecodeTest,
                         ::testing::ValuesIn(kAudioFixtures),
                         [](const auto& info) {
                           std::string name(info.param.file);
                           std::replace(name.begin(), name.end(), '.', '_');
                           return name;
                         });

TEST(AudioDecodeDetailTest, FlacPcmMatchesTheMd5InItsStreamInfo) {
  const ClipStream stream = Load("flac.flac", StreamType::kAudio);
  const auto info =
      umbra::codec::ParseFlacStreamInfo(stream.info.codec_config.span());
  ASSERT_TRUE(info.ok()) << info.status();
  Session session("ffmpeg.flac", stream);
  ASSERT_TRUE(session.status().ok());
  const DecodeOutput output = session.Run(stream.samples);
  std::vector<uint8_t> pcm;
  for (const DecodedAudioBuffer& buffer : output.audio) {
    EXPECT_EQ(buffer.format, SampleFormat::kS16);
    const auto bytes = decode_test::InterleavedPcm(buffer);
    pcm.insert(pcm.end(), bytes.begin(), bytes.end());
  }
  EXPECT_EQ(TotalSamples(output.audio),
            static_cast<int64_t>(info->total_samples));
  std::string expected;
  for (const uint8_t byte : info->md5) {
    expected += absl::StrCat(absl::Hex(byte, absl::kZeroPad2));
  }
  EXPECT_EQ(decode_test::Md5Hex(pcm), expected);
}

TEST(AudioDecodeDetailTest, TrimsEncoderPrimingFromTheDemuxerSideData) {
  const ClipStream stream = Load("aac_lc.m4a", StreamType::kAudio);
  ASSERT_TRUE(stream.samples.front().trim_start.is_known());
  const int64_t priming = stream.samples.front().trim_start.value;
  EXPECT_GT(priming, 0);
  ClipStream untrimmed = stream;
  for (EncodedSample& sample : untrimmed.samples) {
    sample.trim_start = {};
    sample.trim_end = {};
  }
  Session trimmed_session("ffmpeg.aac", stream);
  Session untrimmed_session("ffmpeg.aac", untrimmed);
  ASSERT_TRUE(trimmed_session.status().ok());
  ASSERT_TRUE(untrimmed_session.status().ok());
  const int64_t with_trim =
      TotalSamples(trimmed_session.Run(stream.samples).audio);
  const int64_t without_trim =
      TotalSamples(untrimmed_session.Run(untrimmed.samples).audio);
  EXPECT_GT(without_trim, with_trim);
  EXPECT_GE(without_trim - with_trim, priming);
}

TEST(ConfigChangeTest, AcceptsAnUnchangedConfigurationInPlace) {
  const ClipStream stream = Load("h264_high_bframes.mp4", StreamType::kVideo);
  Session session("ffmpeg.h264", stream);
  ASSERT_TRUE(session.status().ok());
  std::vector<EncodedSample> samples = stream.samples;
  samples[3].config_change = std::make_shared<StreamInfo>(stream.info);
  const DecodeOutput output = session.Run(samples);
  EXPECT_TRUE(output.end_of_stream);
  EXPECT_THAT(output.errors, IsEmpty());
  EXPECT_EQ(Digests(output.video),
            decode_test::ReadFrameMd5("h264_high_bframes.mp4.framemd5"));
}

TEST(ConfigChangeTest, RefusesACodecChangeWithUnimplemented) {
  const ClipStream stream = Load("h264_high_bframes.mp4", StreamType::kVideo);
  Session session("ffmpeg.h264", stream);
  ASSERT_TRUE(session.status().ok());
  auto other = std::make_shared<StreamInfo>(stream.info);
  other->codec = CodecId::kHevc;
  std::vector<EncodedSample> samples = {stream.samples.front()};
  samples.front().config_change = other;
  const DecodeOutput output = session.Run(samples, 1, false);
  ASSERT_EQ(output.errors.size(), 1U);
  EXPECT_EQ(output.errors.front().status.code(),
            absl::StatusCode::kUnimplemented);
}

TEST(ConfigChangeTest, RefusesANewAudioConfigurationWithUnimplemented) {
  const ClipStream stream = Load("aac_lc.m4a", StreamType::kAudio);
  Session session("ffmpeg.aac", stream);
  ASSERT_TRUE(session.status().ok());
  auto other = std::make_shared<StreamInfo>(stream.info);
  other->codec_config = SharedBytes::Copy(std::vector<uint8_t>{0x11, 0x90});
  std::vector<EncodedSample> samples = {stream.samples.front()};
  samples.front().config_change = other;
  const DecodeOutput output = session.Run(samples, 1, false);
  ASSERT_EQ(output.errors.size(), 1U);
  EXPECT_EQ(output.errors.front().status.code(),
            absl::StatusCode::kUnimplemented);
}

TEST(CapabilityTest, ReportsAFramePixelFormatItCannotDescribeAsAFatalError) {
  const ClipStream stream = Load("h264_high422.mp4", StreamType::kVideo);
  Session session("ffmpeg.h264", stream);
  ASSERT_TRUE(session.status().ok());
  const DecodeOutput output = session.Run(stream.samples);
  EXPECT_TRUE(output.video.empty());
  ASSERT_FALSE(output.errors.empty());
  EXPECT_EQ(output.errors.front().status.code(),
            absl::StatusCode::kUnimplemented);
  EXPECT_EQ(output.errors.front().category, PortErrorCategory::kCapability);
  EXPECT_EQ(output.errors.front().recovery, RecoveryHint::kFatal);
}

TEST(WriteContractTest, RejectsWritesThatBreakTheGrantProtocol) {
  const ClipStream stream = Load("h264_high_bframes.mp4", StreamType::kVideo);
  Session session("ffmpeg.h264", stream);
  ASSERT_TRUE(session.status().ok());
  const auto first = session.events().Next(std::chrono::seconds(10));
  ASSERT_TRUE(first.has_value());
  const auto* grant = std::get_if<NeedsData>(&first->detail);
  ASSERT_NE(grant, nullptr);
  EXPECT_EQ(grant->track_id, stream.info.track_id);
  EXPECT_GT(grant->max_samples, 0U);
  const std::span<const EncodedSample> all = stream.samples;
  EXPECT_EQ(session.decoder().WriteSamples(7, all.first(1)).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(session.decoder().WriteSamples(1, {}).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(session.decoder()
                .WriteSamples(1, all.first(grant->max_samples + 1))
                .code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(session.decoder().WriteSamples(1, all.first(1)).ok());
  EXPECT_EQ(session.decoder().WriteSamples(1, all.subspan(1, 1)).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(session.decoder().Flush(1).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(WriteContractTest, EndOfStreamNeedsAGrantAndStopIsIdempotent) {
  const ClipStream stream = Load("flac.flac", StreamType::kAudio);
  Session session("ffmpeg.flac", stream);
  ASSERT_TRUE(session.status().ok());
  const auto first = session.events().Next(std::chrono::seconds(10));
  ASSERT_TRUE(first.has_value());
  EXPECT_TRUE(session.decoder().EndOfStream(1).ok());
  EXPECT_EQ(session.decoder().EndOfStream(1).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_TRUE(session.decoder().Stop().ok());
  EXPECT_TRUE(session.decoder().Stop().ok());
  EXPECT_EQ(session.decoder().Flush(2).code(),
            absl::StatusCode::kFailedPrecondition);
}

struct Mutation {
  std::string_view file;
  StreamType type;
  std::string_view decoder;
};

class CorruptionTest : public ::testing::TestWithParam<Mutation> {};

TEST_P(CorruptionTest, CorruptSamplesProduceErrorsAndNeverCrash) {
  const Mutation mutation = GetParam();
  const ClipStream clean = Load(mutation.file, mutation.type);
  std::mt19937 random(0x5eed);
  size_t iterations_with_errors = 0;
  constexpr int kIterations = 24;
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    ClipStream stream = clean;
    const size_t damaged = 1 + random() % 4;
    for (size_t round = 0; round < damaged; ++round) {
      EncodedSample& sample = stream.samples[random() % stream.samples.size()];
      std::vector<uint8_t> bytes(sample.data.span().begin(),
                                 sample.data.span().end());
      switch (random() % 3) {
        case 0:
          for (size_t flips = 1 + random() % 8; flips > 0; --flips) {
            bytes[random() % bytes.size()] ^=
                static_cast<uint8_t>(1U << (random() % 8));
          }
          break;
        case 1:
          bytes.resize(1 + random() % bytes.size());
          break;
        default:
          std::fill(
              bytes.begin() + static_cast<ptrdiff_t>(random() % bytes.size()),
              bytes.end(), 0);
          break;
      }
      sample.data = SharedBytes::Adopt(std::move(bytes));
    }
    Session session(mutation.decoder, stream, 1);
    ASSERT_TRUE(session.status().ok());
    const DecodeOutput output = session.Run(stream.samples);
    EXPECT_FALSE(output.timed_out) << "iteration " << iteration;
    for (const PortError& error : output.errors) {
      EXPECT_FALSE(error.status.ok());
      if (error.status.code() == absl::StatusCode::kDataLoss) {
        EXPECT_EQ(error.recovery, RecoveryHint::kTransient);
        EXPECT_EQ(error.category, PortErrorCategory::kDecode);
      }
    }
    if (!output.errors.empty()) {
      ++iterations_with_errors;
    }
    EXPECT_TRUE(output.end_of_stream ||
                (!output.errors.empty() &&
                 output.errors.back().recovery != RecoveryHint::kTransient))
        << "iteration " << iteration;
  }
  RecordProperty("iterations_with_errors",
                 static_cast<int>(iterations_with_errors));
}

INSTANTIATE_TEST_SUITE_P(
    Fixtures, CorruptionTest,
    ::testing::Values(
        Mutation{"h264_high_bframes.mp4", StreamType::kVideo, "ffmpeg.h264"},
        Mutation{"hevc_main_bframes.mp4", StreamType::kVideo, "ffmpeg.hevc"},
        Mutation{"vp9_profile0.webm", StreamType::kVideo, "ffmpeg.vp9"},
        Mutation{"aac_lc.m4a", StreamType::kAudio, "ffmpeg.aac"},
        Mutation{"opus.ogg", StreamType::kAudio, "ffmpeg.opus"},
        Mutation{"mp3.mp3", StreamType::kAudio, "ffmpeg.mp3float"},
        Mutation{"ac3.ac3", StreamType::kAudio, "ffmpeg.ac3"},
        Mutation{"eac3.eac3", StreamType::kAudio, "ffmpeg.eac3"},
        Mutation{"flac.flac", StreamType::kAudio, "ffmpeg.flac"}),
    [](const auto& info) {
      std::string name(info.param.file);
      std::replace(name.begin(), name.end(), '.', '_');
      return name;
    });

TEST(CorruptionDetailTest, AMalformedSampleIsADataLossErrorWithATransientHint) {
  const ClipStream stream = Load("flac.flac", StreamType::kAudio);
  Session session("ffmpeg.flac", stream);
  ASSERT_TRUE(session.status().ok());
  std::vector<EncodedSample> samples = stream.samples;
  samples[2].data = SharedBytes::Copy(std::vector<uint8_t>(64, 0xA5));
  const DecodeOutput output = session.Run(samples);
  EXPECT_TRUE(output.end_of_stream);
  ASSERT_FALSE(output.errors.empty());
  EXPECT_EQ(output.errors.front().status.code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(output.errors.front().recovery, RecoveryHint::kTransient);
  EXPECT_THAT(std::string(output.errors.front().status.message()),
              ::testing::StartsWith("ALUMBRA-3001"));
}

}
}
