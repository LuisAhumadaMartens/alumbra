#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "penumbra/desktop/decode_session.h"
#include "penumbra/desktop/decode_test_util.h"
#include "penumbra/desktop/decoder_probe.h"
#include "penumbra/desktop/desktop_component_factory.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/encryption_info.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
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
using decode_test::ConfigOf;
using decode_test::DecodeOutput;
using decode_test::Digests;
using decode_test::EventCollector;
using decode_test::Load;
using decode_test::Session;
using ::testing::IsEmpty;

bool SilenceFfmpegLogs() {
  av_log_set_level(AV_LOG_QUIET);
  return true;
}

[[maybe_unused]] const bool kLogsSilenced = SilenceFfmpegLogs();

struct VideoFixture {
  std::string_view file;
  std::string_view decoder;
  PixelFormat format;
};

const std::array<VideoFixture, 6> kVideoFixtures = {{
    {.file = "h264_high_bframes.mp4",
     .decoder = "ffmpeg.h264",
     .format = PixelFormat::kI420},
    {.file = "h264_baseline.h264",
     .decoder = "ffmpeg.h264",
     .format = PixelFormat::kI420},
    {.file = "hevc_main_bframes.mp4",
     .decoder = "ffmpeg.hevc",
     .format = PixelFormat::kI420},
    {.file = "hevc_main10_bframes.mp4",
     .decoder = "ffmpeg.hevc",
     .format = PixelFormat::kI010},
    {.file = "vp9_profile0.webm",
     .decoder = "ffmpeg.vp9",
     .format = PixelFormat::kI420},
    {.file = "vp9_profile2.webm",
     .decoder = "ffmpeg.vp9",
     .format = PixelFormat::kI010},
}};

struct AudioFixture {
  std::string_view file;
  std::string_view decoder;
};

const std::array<AudioFixture, 6> kAudioFixtures = {{
    {.file = "aac_lc.m4a", .decoder = "ffmpeg.aac"},
    {.file = "opus.ogg", .decoder = "ffmpeg.opus"},
    {.file = "mp3.mp3", .decoder = "ffmpeg.mp3float"},
    {.file = "ac3.ac3", .decoder = "ffmpeg.ac3"},
    {.file = "eac3.eac3", .decoder = "ffmpeg.eac3"},
    {.file = "flac.flac", .decoder = "ffmpeg.flac"},
}};

DesktopDecoderOptions Threads(int32_t threads) {
  DesktopDecoderOptions options;
  options.video_threads = threads;
  return options;
}

int64_t TotalSamples(const std::vector<DecodedAudioBuffer>& audio) {
  int64_t total = 0;
  for (const auto& buffer : audio) {
    total += buffer.frame_count;
  }
  return total;
}

std::string FixtureName(std::string_view file) {
  std::string name(file);
  std::ranges::replace(name, '.', '_');
  return name;
}

void ExpectTickets(const DecodeOutput& output, SeekTicket ticket) {
  for (const SeekTicket seen : output.frame_tickets) {
    EXPECT_EQ(seen, ticket);
  }
}

std::vector<std::string> NamesOf(const std::vector<DecoderCandidate>& list) {
  std::vector<std::string> names;
  names.reserve(list.size());
  for (const DecoderCandidate& candidate : list) {
    names.push_back(candidate.name);
  }
  return names;
}

TEST(FactoryTest, ListsTheBundledSoftwareCandidatesForEachStreamType) {
  const DesktopComponentFactory factory;
  const auto video = factory.GetDecoderCandidates(StreamType::kVideo);
  ASSERT_TRUE(video.ok());
  for (const DecoderCandidate& candidate : *video) {
    EXPECT_EQ(candidate.kind, DecoderKind::kBundledSoftware);
    EXPECT_EQ(candidate.output, DecoderOutput::kCpuBuffer);
    EXPECT_FALSE(candidate.secure);
    EXPECT_EQ(candidate.codecs.size(), 1U);
    EXPECT_EQ(candidate.max_width, 0);
  }
  EXPECT_THAT(
      NamesOf(*video),
      ::testing::IsSupersetOf({"ffmpeg.h264", "ffmpeg.hevc", "ffmpeg.vp9"}));
  const auto audio = factory.GetDecoderCandidates(StreamType::kAudio);
  ASSERT_TRUE(audio.ok());
  EXPECT_THAT(NamesOf(*audio),
              ::testing::UnorderedElementsAre("ffmpeg.aac", "ffmpeg.opus",
                                              "ffmpeg.mp3float", "ffmpeg.ac3",
                                              "ffmpeg.eac3", "ffmpeg.flac"));
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
  const DesktopComponentFactory factory(
      {}, std::make_shared<MissingDecoderProbe>());
  const auto video = factory.GetDecoderCandidates(StreamType::kVideo);
  ASSERT_TRUE(video.ok());
  EXPECT_THAT(NamesOf(*video),
              ::testing::Not(::testing::Contains("ffmpeg.hevc")));
}

TEST(FactoryTest, RejectsUnknownCandidatesAndMismatchedCodecs) {
  const ClipStream stream = Load("h264_high_bframes.mp4", StreamType::kVideo);
  DesktopComponentFactory factory;
  const auto events = std::make_shared<EventCollector>();
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
    const Session session("ffmpeg.h264", stream, Threads(threads));
    EXPECT_EQ(session.Status().code(), absl::StatusCode::kInvalidArgument)
        << threads;
  }
  for (const int32_t threads : {0, 1, 64}) {
    const Session session("ffmpeg.h264", stream, Threads(threads));
    EXPECT_TRUE(session.Status().ok()) << threads << session.Status();
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
      decode_test::ReadFrameMd5(absl::StrCat(fixture.file, ".framemd5"));
  ASSERT_FALSE(expected.empty());
  for (const int32_t threads : {1, 0, 4}) {
    const Session session(fixture.decoder, stream, Threads(threads));
    ASSERT_TRUE(session.Status().ok()) << session.Status();
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
  const Session session(fixture.decoder, stream);
  ASSERT_TRUE(session.Status().ok());
  const DecodeOutput output = session.Run(stream.samples);
  std::vector<int64_t> actual;
  actual.reserve(output.video.size());
  for (const DecodedVideoFrame& frame : output.video) {
    actual.push_back(frame.presentation_time.InMicroseconds());
    EXPECT_GT(frame.duration.InMicroseconds(), 0);
  }
  EXPECT_EQ(actual, decode_test::SortedPresentationMicros(stream));
}

TEST_P(VideoDecodeTest, FlushDeliversNoStaleFramesAndRestartsAtAKeyFrame) {
  const VideoFixture fixture = GetParam();
  const ClipStream stream = Load(fixture.file, StreamType::kVideo);
  const size_t second_key = decode_test::SecondKeyFrameIndex(stream);
  ASSERT_GT(second_key, 0U);
  const Session session(fixture.decoder, stream);
  ASSERT_TRUE(session.Status().ok());
  const DecodeOutput before = session.Run(stream.samples, 1, false, 1);
  EXPECT_FALSE(before.timed_out);
  ExpectTickets(before, 1);
  ASSERT_TRUE(session.Underlying().Flush(2).ok());
  const std::span<const EncodedSample> tail =
      std::span<const EncodedSample>(stream.samples).subspan(second_key);
  const DecodeOutput after = session.Run(tail, 2);
  EXPECT_TRUE(after.end_of_stream);
  EXPECT_THAT(after.errors, IsEmpty());
  ExpectTickets(after, 2);
  ClipStream tail_stream;
  tail_stream.info = stream.info;
  tail_stream.samples.assign(tail.begin(), tail.end());
  const auto reference = decode_test::ReferenceDecode(tail_stream);
  ASSERT_TRUE(reference.ok());
  EXPECT_EQ(Digests(after.video), Digests(reference->video));
  EXPECT_FALSE(after.video.empty());
}

INSTANTIATE_TEST_SUITE_P(Fixtures, VideoDecodeTest,
                         ::testing::ValuesIn(kVideoFixtures),
                         [](const auto& info) {
                           return FixtureName(info.param.file);
                         });

class AudioDecodeTest : public ::testing::TestWithParam<AudioFixture> {};

void ExpectAudioBuffer(const DecodedAudioBuffer& buffer, int64_t previous) {
  EXPECT_EQ(buffer.sample_rate, 48000);
  EXPECT_EQ(buffer.channel_count, 2);
  EXPECT_NE(buffer.presentation_time, MediaTime::Unknown());
  EXPECT_GT(buffer.presentation_time.InMicroseconds(), previous);
}

void ExpectSteadyAudio(const DecodeOutput& output) {
  int64_t previous = INT64_MIN;
  for (const DecodedAudioBuffer& buffer : output.audio) {
    ExpectAudioBuffer(buffer, previous);
    previous = buffer.presentation_time.InMicroseconds();
  }
  EXPECT_GE(TotalSamples(output.audio), 44000);
  EXPECT_LE(TotalSamples(output.audio), 50000);
}

TEST_P(AudioDecodeTest, MatchesTheReferenceDecode) {
  const AudioFixture fixture = GetParam();
  const ClipStream stream = Load(fixture.file, StreamType::kAudio);
  const auto reference = decode_test::ReferenceDecode(stream);
  ASSERT_TRUE(reference.ok()) << reference.status();
  const Session session(fixture.decoder, stream);
  ASSERT_TRUE(session.Status().ok()) << session.Status();
  const DecodeOutput output = session.Run(stream.samples);
  EXPECT_FALSE(output.timed_out);
  EXPECT_TRUE(output.end_of_stream);
  EXPECT_THAT(output.errors, IsEmpty());
  EXPECT_EQ(Digests(output.audio), Digests(reference->audio));
  ASSERT_FALSE(output.audio.empty());
  ExpectSteadyAudio(output);
}

TEST_P(AudioDecodeTest, FlushRestartsDecodingWithANewTicket) {
  const AudioFixture fixture = GetParam();
  const ClipStream stream = Load(fixture.file, StreamType::kAudio);
  const Session session(fixture.decoder, stream);
  ASSERT_TRUE(session.Status().ok());
  const DecodeOutput first = session.Run(stream.samples, 1, false, 1);
  EXPECT_FALSE(first.timed_out);
  ASSERT_TRUE(session.Underlying().Flush(2).ok());
  const DecodeOutput second = session.Run(stream.samples, 2);
  EXPECT_TRUE(second.end_of_stream);
  ExpectTickets(second, 2);
  const auto reference = decode_test::ReferenceDecode(stream);
  ASSERT_TRUE(reference.ok());
  EXPECT_EQ(TotalSamples(second.audio), TotalSamples(reference->audio));
}

INSTANTIATE_TEST_SUITE_P(Fixtures, AudioDecodeTest,
                         ::testing::ValuesIn(kAudioFixtures),
                         [](const auto& info) {
                           return FixtureName(info.param.file);
                         });

std::string HexOf(const std::array<uint8_t, 16>& digest) {
  std::string text;
  for (const uint8_t byte : digest) {
    absl::StrAppend(&text, absl::Hex(byte, absl::kZeroPad2));
  }
  return text;
}

TEST(AudioDecodeDetailTest, FlacPcmMatchesTheMd5InItsStreamInfo) {
  const ClipStream stream = Load("flac.flac", StreamType::kAudio);
  const auto info =
      umbra::codec::ParseFlacStreamInfo(stream.info.codec_config.span());
  ASSERT_TRUE(info.ok()) << info.status();
  const Session session("ffmpeg.flac", stream);
  ASSERT_TRUE(session.Status().ok());
  const DecodeOutput output = session.Run(stream.samples);
  std::vector<uint8_t> pcm;
  for (const DecodedAudioBuffer& buffer : output.audio) {
    EXPECT_EQ(buffer.format, SampleFormat::kS16);
    const auto bytes = decode_test::InterleavedPcm(buffer);
    pcm.insert(pcm.end(), bytes.begin(), bytes.end());
  }
  EXPECT_EQ(TotalSamples(output.audio),
            static_cast<int64_t>(info->total_samples));
  EXPECT_EQ(decode_test::Md5Hex(pcm), HexOf(info->md5));
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
  const Session trimmed_session("ffmpeg.aac", stream);
  const Session untrimmed_session("ffmpeg.aac", untrimmed);
  ASSERT_TRUE(trimmed_session.Status().ok());
  ASSERT_TRUE(untrimmed_session.Status().ok());
  const int64_t with_trim =
      TotalSamples(trimmed_session.Run(stream.samples).audio);
  const int64_t without_trim =
      TotalSamples(untrimmed_session.Run(untrimmed.samples).audio);
  EXPECT_GT(without_trim, with_trim);
  EXPECT_GE(without_trim - with_trim, priming);
}

TEST(ConfigChangeTest, AcceptsAnUnchangedConfigurationInPlace) {
  const ClipStream stream = Load("h264_high_bframes.mp4", StreamType::kVideo);
  const Session session("ffmpeg.h264", stream);
  ASSERT_TRUE(session.Status().ok());
  std::vector<EncodedSample> samples = stream.samples;
  samples.at(3).config_change = std::make_shared<StreamInfo>(stream.info);
  const DecodeOutput output = session.Run(samples);
  EXPECT_TRUE(output.end_of_stream);
  EXPECT_THAT(output.errors, IsEmpty());
  EXPECT_EQ(Digests(output.video),
            decode_test::ReadFrameMd5("h264_high_bframes.mp4.framemd5"));
}

void ExpectChangeRefused(const Session& session, const ClipStream& stream,
                         const std::shared_ptr<StreamInfo>& change) {
  ASSERT_TRUE(session.Status().ok());
  std::vector<EncodedSample> samples = {stream.samples.front()};
  samples.front().config_change = change;
  const DecodeOutput output = session.Run(samples, 1, false);
  ASSERT_EQ(output.errors.size(), 1U);
  EXPECT_EQ(output.errors.front().status.code(),
            absl::StatusCode::kUnimplemented);
}

TEST(ConfigChangeTest, RefusesACodecChangeWithUnimplemented) {
  const ClipStream stream = Load("h264_high_bframes.mp4", StreamType::kVideo);
  const Session session("ffmpeg.h264", stream);
  auto other = std::make_shared<StreamInfo>(stream.info);
  other->codec = CodecId::kHevc;
  ExpectChangeRefused(session, stream, other);
}

TEST(ConfigChangeTest, RefusesANewAudioConfigurationWithUnimplemented) {
  const ClipStream stream = Load("aac_lc.m4a", StreamType::kAudio);
  const Session session("ffmpeg.aac", stream);
  auto other = std::make_shared<StreamInfo>(stream.info);
  other->codec_config = SharedBytes::Copy(std::vector<uint8_t>{0x11, 0x90});
  ExpectChangeRefused(session, stream, other);
}

TEST(CapabilityTest, ReportsAFramePixelFormatItCannotDescribeAsAFatalError) {
  const ClipStream stream = Load("h264_high422.mp4", StreamType::kVideo);
  const Session session("ffmpeg.h264", stream);
  ASSERT_TRUE(session.Status().ok());
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
  const Session session("ffmpeg.h264", stream);
  ASSERT_TRUE(session.Status().ok());
  const auto first = session.Events().Next(std::chrono::seconds(10));
  ASSERT_TRUE(first.has_value());
  const auto* grant =
      first.has_value() ? std::get_if<NeedsData>(&first->detail) : nullptr;
  ASSERT_NE(grant, nullptr);
  EXPECT_EQ(grant->track_id, stream.info.track_id);
  EXPECT_GT(grant->max_samples, 0U);
  const std::span<const EncodedSample> all = stream.samples;
  Decoder& decoder = session.Underlying();
  EXPECT_EQ(decoder.WriteSamples(7, all.first(1)).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decoder.WriteSamples(1, {}).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decoder.WriteSamples(1, all.first(grant->max_samples + 1)).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(decoder.WriteSamples(1, all.first(1)).ok());
  EXPECT_EQ(decoder.WriteSamples(1, all.subspan(1, 1)).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(decoder.Flush(1).code(), absl::StatusCode::kInvalidArgument);
}

TEST(WriteContractTest, EndOfStreamNeedsAGrantAndStopIsIdempotent) {
  const ClipStream stream = Load("flac.flac", StreamType::kAudio);
  const Session session("ffmpeg.flac", stream);
  ASSERT_TRUE(session.Status().ok());
  const auto first = session.Events().Next(std::chrono::seconds(10));
  ASSERT_TRUE(first.has_value());
  Decoder& decoder = session.Underlying();
  EXPECT_TRUE(decoder.EndOfStream(1).ok());
  EXPECT_EQ(decoder.EndOfStream(1).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_TRUE(decoder.Stop().ok());
  EXPECT_TRUE(decoder.Stop().ok());
  EXPECT_EQ(decoder.Flush(2).code(), absl::StatusCode::kFailedPrecondition);
}

struct Mutation {
  std::string_view file;
  StreamType type;
  std::string_view decoder;
};

const std::array<Mutation, 9> kMutations = {{
    {.file = "h264_high_bframes.mp4",
     .type = StreamType::kVideo,
     .decoder = "ffmpeg.h264"},
    {.file = "hevc_main_bframes.mp4",
     .type = StreamType::kVideo,
     .decoder = "ffmpeg.hevc"},
    {.file = "vp9_profile0.webm",
     .type = StreamType::kVideo,
     .decoder = "ffmpeg.vp9"},
    {.file = "aac_lc.m4a", .type = StreamType::kAudio, .decoder = "ffmpeg.aac"},
    {.file = "opus.ogg", .type = StreamType::kAudio, .decoder = "ffmpeg.opus"},
    {.file = "mp3.mp3",
     .type = StreamType::kAudio,
     .decoder = "ffmpeg.mp3float"},
    {.file = "ac3.ac3", .type = StreamType::kAudio, .decoder = "ffmpeg.ac3"},
    {.file = "eac3.eac3", .type = StreamType::kAudio, .decoder = "ffmpeg.eac3"},
    {.file = "flac.flac", .type = StreamType::kAudio, .decoder = "ffmpeg.flac"},
}};

void ExpectDocumentedError(const PortError& error) {
  EXPECT_FALSE(error.status.ok());
  if (error.status.code() == absl::StatusCode::kDataLoss) {
    EXPECT_EQ(error.recovery, RecoveryHint::kTransient);
    EXPECT_EQ(error.category, PortErrorCategory::kDecode);
  }
}

void ExpectOnlyDocumentedErrors(const DecodeOutput& output) {
  EXPECT_FALSE(output.timed_out);
  for (const PortError& error : output.errors) {
    ExpectDocumentedError(error);
  }
  const bool ended_fatally =
      !output.errors.empty() &&
      output.errors.back().recovery != RecoveryHint::kTransient;
  EXPECT_TRUE(output.end_of_stream || ended_fatally);
}

class CorruptionTest : public ::testing::TestWithParam<Mutation> {};

TEST_P(CorruptionTest, CorruptSamplesProduceErrorsAndNeverCrash) {
  const Mutation mutation = GetParam();
  const ClipStream clean = Load(mutation.file, mutation.type);
  decode_test::DeterministicRandom random(0x5eed);
  for (int iteration = 0; iteration < 24; ++iteration) {
    ClipStream stream = clean;
    decode_test::CorruptSamples(stream, random);
    const Session session(mutation.decoder, stream, Threads(1));
    ASSERT_TRUE(session.Status().ok());
    ExpectOnlyDocumentedErrors(session.Run(stream.samples));
  }
}

INSTANTIATE_TEST_SUITE_P(Fixtures, CorruptionTest,
                         ::testing::ValuesIn(kMutations), [](const auto& info) {
                           return FixtureName(info.param.file);
                         });

TEST(CorruptionDetailTest, AMalformedSampleIsADataLossErrorWithATransientHint) {
  const ClipStream stream = Load("flac.flac", StreamType::kAudio);
  const Session session("ffmpeg.flac", stream);
  ASSERT_TRUE(session.Status().ok());
  std::vector<EncodedSample> samples = stream.samples;
  samples.at(2).data = SharedBytes::Copy(std::vector<uint8_t>(64, 0xA5));
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
