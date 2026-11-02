#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "penumbra/desktop/decode_session.h"
#include "penumbra/desktop/decode_test_util.h"
#include "penumbra/desktop/desktop_component_factory.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra/reference_player/decoder_selection.h"

namespace penumbra::desktop {
namespace {

using decode_test::ClipStream;
using decode_test::DecodeOutput;
using decode_test::Digests;
using decode_test::Load;
using decode_test::Session;
using ::testing::IsEmpty;

struct Vector {
  std::string_view name;
  PixelFormat format;
  bool has_grain;
};

constexpr Vector kVectors[] = {
    {"av1-1-b8-01-size-16x16", PixelFormat::kI420, false},
    {"av1-1-b8-01-size-66x66", PixelFormat::kI420, false},
    {"av1-1-b8-04-cdfupdate", PixelFormat::kI420, false},
    {"av1-1-b8-23-film_grain-50", PixelFormat::kI420, true},
    {"av1-1-b10-23-film_grain-50", PixelFormat::kI010, true},
};

DesktopDecoderOptions Options(int32_t threads, bool grain,
                              int32_t frame_delay = 0) {
  DesktopDecoderOptions options;
  options.video_threads = threads;
  options.av1_apply_film_grain = grain;
  options.av1_max_frame_delay = frame_delay;
  return options;
}

std::string Path(std::string_view name, std::string_view suffix) {
  return std::string("av1/") + std::string(name) + std::string(suffix);
}

TEST(Dav1dFactoryTest, IsTheFirstAv1CandidateAndSelectedByDefault) {
  const ClipStream stream = Load("av1_main.mp4", StreamType::kVideo);
  DesktopComponentFactory factory;
  const auto candidates = factory.GetDecoderCandidates(StreamType::kVideo);
  ASSERT_TRUE(candidates.ok());
  ASSERT_FALSE(candidates->empty());
  const DecoderCandidate& first = candidates->front();
  EXPECT_EQ(first.name, "dav1d");
  EXPECT_EQ(first.kind, DecoderKind::kBundledSoftware);
  EXPECT_EQ(first.output, DecoderOutput::kCpuBuffer);
  EXPECT_EQ(first.codecs, std::vector<CodecId>{CodecId::kAv1});
  SelectionRequest request;
  request.stream = decode_test::ConfigOf(stream);
  const auto selected = SelectDecoderCandidates(*candidates, request);
  ASSERT_FALSE(selected.empty());
  EXPECT_EQ(selected.front().name, "dav1d");
  for (const DecoderCandidate& candidate : selected) {
    EXPECT_NE(candidate.name.rfind("ffmpeg.", 0), 0U)
        << "an FFmpeg decoder must not decode AV1";
  }
}

TEST(Dav1dFactoryTest, ValidatesItsOptions) {
  const ClipStream stream = Load("av1_main.mp4", StreamType::kVideo);
  EXPECT_EQ(Session("dav1d", stream, Options(65, true)).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(Session("dav1d", stream, Options(0, true, 257)).status().code(),
            absl::StatusCode::kInvalidArgument);
  DesktopDecoderOptions bad_point;
  bad_point.av1_operating_point = 32;
  EXPECT_EQ(Session("dav1d", stream, bad_point).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(Session("dav1d", stream, Options(64, false, 256)).status().ok());
}

class Av1VectorTest : public ::testing::TestWithParam<Vector> {};

TEST_P(Av1VectorTest, MatchesTheLibaomPerFrameMd5WithFilmGrain) {
  const Vector vector = GetParam();
  const ClipStream stream = Load(Path(vector.name, ".ivf"), StreamType::kVideo);
  const auto expected =
      decode_test::ReadLibaomMd5(Path(vector.name, ".ivf.md5"));
  ASSERT_FALSE(expected.empty());
  Session session("dav1d", stream);
  ASSERT_TRUE(session.status().ok()) << session.status();
  const DecodeOutput output = session.Run(stream.samples);
  EXPECT_TRUE(output.end_of_stream);
  EXPECT_THAT(output.errors, IsEmpty());
  EXPECT_EQ(Digests(output.video), expected);
  for (const DecodedVideoFrame& frame : output.video) {
    EXPECT_EQ(frame.format, vector.format);
  }
}

TEST_P(Av1VectorTest, MatchesTheDav1dMd5ReferencesWithFilmGrainOnAndOff) {
  const Vector vector = GetParam();
  const ClipStream stream = Load(Path(vector.name, ".ivf"), StreamType::kVideo);
  for (const bool grain : {true, false}) {
    const std::string expected = decode_test::ReadSingleMd5(Path(
        vector.name, grain ? ".dav1d-grain-on.md5" : ".dav1d-grain-off.md5"));
    for (const int32_t threads : {1, 0, 4}) {
      Session session("dav1d", stream, Options(threads, grain));
      ASSERT_TRUE(session.status().ok()) << session.status();
      const DecodeOutput output = session.Run(stream.samples);
      EXPECT_TRUE(output.end_of_stream);
      EXPECT_EQ(decode_test::DigestVideoStream(output.video), expected)
          << "grain " << grain << " threads " << threads;
    }
  }
}

TEST_P(Av1VectorTest, LowLatencyFrameDelayDoesNotChangeTheOutput) {
  const Vector vector = GetParam();
  const ClipStream stream = Load(Path(vector.name, ".ivf"), StreamType::kVideo);
  Session session("dav1d", stream, Options(4, true, 1));
  ASSERT_TRUE(session.status().ok());
  const DecodeOutput output = session.Run(stream.samples);
  EXPECT_EQ(
      decode_test::DigestVideoStream(output.video),
      decode_test::ReadSingleMd5(Path(vector.name, ".dav1d-grain-on.md5")));
}

INSTANTIATE_TEST_SUITE_P(LibaomVectors, Av1VectorTest,
                         ::testing::ValuesIn(kVectors), [](const auto& info) {
                           std::string name(info.param.name);
                           std::replace(name.begin(), name.end(), '-', '_');
                           return name;
                         });

TEST(Dav1dDecodeTest, FilmGrainChangesThePicturesOnlyWhenEnabled) {
  const ClipStream stream =
      Load("av1/av1-1-b8-23-film_grain-50.ivf", StreamType::kVideo);
  Session with_grain("dav1d", stream, Options(1, true));
  Session without_grain("dav1d", stream, Options(1, false));
  const auto on =
      decode_test::DigestVideoStream(with_grain.Run(stream.samples).video);
  const auto off =
      decode_test::DigestVideoStream(without_grain.Run(stream.samples).video);
  EXPECT_NE(on, off);
}

TEST(Dav1dDecodeTest, DecodesMp4SamplesWithTheAv1cSequenceHeader) {
  const ClipStream stream = Load("av1_main.mp4", StreamType::kVideo);
  ASSERT_FALSE(stream.info.codec_config.empty());
  for (const int32_t threads : {1, 0}) {
    Session session("dav1d", stream, Options(threads, true));
    ASSERT_TRUE(session.status().ok()) << session.status();
    const DecodeOutput output = session.Run(stream.samples);
    EXPECT_TRUE(output.end_of_stream);
    EXPECT_THAT(output.errors, IsEmpty());
    EXPECT_EQ(decode_test::DigestVideoStream(output.video),
              decode_test::ReadSingleMd5("av1_main.mp4.dav1d.md5"));
    EXPECT_EQ(output.video.size(), stream.samples.size());
  }
}

TEST(Dav1dDecodeTest, ProducesFramesInPresentationOrderWithTimes) {
  const ClipStream stream = Load("av1_main.mp4", StreamType::kVideo);
  Session session("dav1d", stream);
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
    EXPECT_EQ(frame.width, 160);
    EXPECT_EQ(frame.height, 96);
  }
  EXPECT_EQ(actual, expected);
}

TEST(Dav1dDecodeTest, FlushOnSeekYieldsNoStalePictures) {
  const ClipStream stream = Load("av1_main.mp4", StreamType::kVideo);
  size_t second_key = 0;
  size_t keys = 0;
  for (size_t index = 0; index < stream.samples.size(); ++index) {
    if (stream.samples[index].key_frame && ++keys == 2) {
      second_key = index;
    }
  }
  ASSERT_GT(second_key, 0U);
  Session session("dav1d", stream, Options(4, true));
  ASSERT_TRUE(session.status().ok());
  const DecodeOutput before = session.Run(stream.samples, 1, false, 1);
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
  Session fresh("dav1d", stream, Options(1, true));
  const DecodeOutput expected = fresh.Run(tail);
  EXPECT_FALSE(after.video.empty());
  EXPECT_EQ(Digests(after.video), Digests(expected.video));
  const int64_t first_pts =
      ToMediaTime(stream.samples[second_key].pts, Rounding::kNearest)
          .value()
          .InMicroseconds();
  for (const DecodedVideoFrame& frame : after.video) {
    EXPECT_GE(frame.presentation_time.InMicroseconds(), first_pts);
  }
}

TEST(Dav1dDecodeTest, FlushRestartsDecodingOnAnIvfStream) {
  const ClipStream stream =
      Load("av1/av1-1-b8-04-cdfupdate.ivf", StreamType::kVideo);
  Session session("dav1d", stream, Options(0, true));
  ASSERT_TRUE(session.status().ok());
  (void)session.Run(stream.samples, 1, false, 1);
  ASSERT_TRUE(session.decoder().Flush(2).ok());
  const DecodeOutput after = session.Run(stream.samples, 2);
  EXPECT_EQ(decode_test::DigestVideoStream(after.video),
            decode_test::ReadSingleMd5(
                "av1/av1-1-b8-04-cdfupdate.dav1d-grain-on.md5"));
}

TEST(Dav1dDecodeTest,
     AcceptsAConfigurationChangeInPlaceAndRefusesACodecChange) {
  const ClipStream stream = Load("av1_main.mp4", StreamType::kVideo);
  Session session("dav1d", stream);
  ASSERT_TRUE(session.status().ok());
  std::vector<EncodedSample> samples = stream.samples;
  samples[0].config_change = std::make_shared<StreamInfo>(stream.info);
  const DecodeOutput output = session.Run(samples);
  EXPECT_THAT(output.errors, IsEmpty());
  EXPECT_EQ(decode_test::DigestVideoStream(output.video),
            decode_test::ReadSingleMd5("av1_main.mp4.dav1d.md5"));

  Session other("dav1d", stream);
  auto changed = std::make_shared<StreamInfo>(stream.info);
  changed->codec = CodecId::kH264;
  std::vector<EncodedSample> first = {stream.samples.front()};
  first.front().config_change = changed;
  const DecodeOutput refused = other.Run(first, 1, false);
  ASSERT_EQ(refused.errors.size(), 1U);
  EXPECT_EQ(refused.errors.front().status.code(),
            absl::StatusCode::kUnimplemented);
}

TEST(Dav1dDecodeTest, ReportsAnUnsupportedLayoutAsAFatalCapabilityError) {
  const ClipStream stream = Load("av1_yuv444.mp4", StreamType::kVideo);
  Session session("dav1d", stream);
  ASSERT_TRUE(session.status().ok());
  const DecodeOutput output = session.Run(stream.samples);
  EXPECT_TRUE(output.video.empty());
  ASSERT_FALSE(output.errors.empty());
  EXPECT_EQ(output.errors.front().status.code(),
            absl::StatusCode::kUnimplemented);
  EXPECT_EQ(output.errors.front().category, PortErrorCategory::kCapability);
  EXPECT_EQ(output.errors.front().recovery, RecoveryHint::kFatal);
}

class Av1CorruptionTest : public ::testing::TestWithParam<std::string_view> {};

TEST_P(Av1CorruptionTest, CorruptSamplesProduceErrorsAndNeverCrash) {
  const ClipStream clean = Load(GetParam(), StreamType::kVideo);
  std::mt19937 random(0xa11);
  for (int iteration = 0; iteration < 24; ++iteration) {
    ClipStream stream = clean;
    for (size_t round = 1 + random() % 4; round > 0; --round) {
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
          std::fill(bytes.begin() +
                        static_cast<std::ptrdiff_t>(random() % bytes.size()),
                    bytes.end(), 0);
          break;
      }
      sample.data = SharedBytes::Adopt(std::move(bytes));
    }
    Session session("dav1d", stream);
    ASSERT_TRUE(session.status().ok());
    const DecodeOutput output = session.Run(stream.samples);
    EXPECT_FALSE(output.timed_out) << "iteration " << iteration;
    for (const PortError& error : output.errors) {
      EXPECT_FALSE(error.status.ok());
      if (error.status.code() == absl::StatusCode::kDataLoss) {
        EXPECT_EQ(error.recovery, RecoveryHint::kTransient);
      }
    }
    EXPECT_TRUE(output.end_of_stream ||
                (!output.errors.empty() &&
                 output.errors.back().recovery != RecoveryHint::kTransient))
        << "iteration " << iteration;
  }
}

INSTANTIATE_TEST_SUITE_P(Clips, Av1CorruptionTest,
                         ::testing::Values("av1_main.mp4",
                                           "av1/av1-1-b8-04-cdfupdate.ivf",
                                           "av1/av1-1-b8-23-film_grain-50.ivf"),
                         [](const auto& info) {
                           std::string name(info.param);
                           std::replace_if(
                               name.begin(), name.end(),
                               [](char c) { return !std::isalnum(c); }, '_');
                           return name;
                         });

}
}
