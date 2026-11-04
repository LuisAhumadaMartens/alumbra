#include "umbra/player/can_play_type.h"

#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"
#include "player_fixture.h"
#include "umbra/player/media_state.h"
#include "umbra/player/player.h"

namespace umbra::player {
namespace {

using test::PlayerFixture;

class RefusingCapabilities final : public penumbra::CapabilityProvider {
 public:
  [[nodiscard]] absl::StatusOr<penumbra::DecodingCapability> QueryDecoding(
      const penumbra::CapabilityRequest& request) const override {
    penumbra::DecodingCapability capability;
    capability.supported = request.stream.codec != penumbra::CodecId::kHevc;
    return capability;
  }
  [[nodiscard]] absl::StatusOr<bool> CanChangeType(
      [[maybe_unused]] const penumbra::StreamConfig& from,
      [[maybe_unused]] const penumbra::StreamConfig& to) const override {
    return false;
  }
};

class CanPlayTypeTest : public PlayerFixture {
 protected:
  CanPlayTypeResult Ask(const char* type) {
    Create();
    return player_->CanPlayType(type);
  }

  static CanPlayTypeResult AskWithoutCapabilities(const char* type) {
    return internal::AnswerCanPlayType(type, nullptr);
  }
};

TEST_F(CanPlayTypeTest, KnownCodecsOnAKnownContainerAreProbablySupported) {
  EXPECT_EQ(Ask("video/mp4; codecs=\"avc1.64001f, mp4a.40.2\""),
            CanPlayTypeResult::kProbably);
}

TEST_F(CanPlayTypeTest, AContainerWithoutCodecsIsOnlyMaybeSupported) {
  EXPECT_EQ(Ask("video/mp4"), CanPlayTypeResult::kMaybe);
  EXPECT_EQ(Ask("audio/webm"), CanPlayTypeResult::kMaybe);
}

TEST_F(CanPlayTypeTest,
       ADanglingOrEmptyCodecsParameterDoesNotExcludeAKnownContainer) {
  for (const char* type :
       {"video/mp4;", "video/mp4; codecs", "video/mp4; codecs="}) {
    EXPECT_EQ(Ask(type), CanPlayTypeResult::kMaybe) << type;
  }
}

TEST_F(CanPlayTypeTest, WithoutACapabilityProviderTheAnswerNeverExceedsMaybe) {
  EXPECT_EQ(AskWithoutCapabilities("video/mp4; codecs=\"avc1.64001f\""),
            CanPlayTypeResult::kMaybe);
}

TEST_F(CanPlayTypeTest, AnUnknownContainerIsNotSupported) {
  EXPECT_EQ(Ask("video/x-unknown"), CanPlayTypeResult::kNone);
  EXPECT_EQ(Ask("application/octet-stream"), CanPlayTypeResult::kNone);
}

TEST_F(CanPlayTypeTest, MalformedTypesAreNotSupported) {
  EXPECT_EQ(Ask(""), CanPlayTypeResult::kNone);
  EXPECT_EQ(Ask("video"), CanPlayTypeResult::kNone);
  EXPECT_EQ(Ask("/mp4"), CanPlayTypeResult::kNone);
  EXPECT_EQ(Ask("video/"), CanPlayTypeResult::kNone);
  EXPECT_EQ(Ask("video/mp4/extra"), CanPlayTypeResult::kNone);
}

TEST_F(CanPlayTypeTest, UnknownParametersAreNotSupported) {
  EXPECT_EQ(Ask("video/mp4; profiles=isom"), CanPlayTypeResult::kNone);
  EXPECT_EQ(Ask("video/mp4; codecs=avc1.64001f; codecs=avc1.64001f"),
            CanPlayTypeResult::kNone);
}

TEST_F(CanPlayTypeTest, AnUnparsableCodecStringIsNotSupported) {
  EXPECT_EQ(Ask("video/mp4; codecs=\"avc1.zz\""), CanPlayTypeResult::kNone);
  EXPECT_EQ(Ask("video/mp4; codecs=\"nonsense\""), CanPlayTypeResult::kNone);
  EXPECT_EQ(Ask("video/mp4; codecs=\"\""), CanPlayTypeResult::kNone);
}

TEST_F(CanPlayTypeTest, ACodecThatTheContainerCannotHoldIsNotSupported) {
  EXPECT_EQ(Ask("video/webm; codecs=\"avc1.64001f\""),
            CanPlayTypeResult::kNone);
  EXPECT_EQ(Ask("audio/mp4; codecs=\"avc1.64001f\""), CanPlayTypeResult::kNone);
  EXPECT_EQ(Ask("video/webm; codecs=\"vp09.00.10.08, opus\""),
            CanPlayTypeResult::kProbably);
}

TEST_F(CanPlayTypeTest, WebmAndMatroskaLegacyCodecsAreRecognised) {
  EXPECT_EQ(Ask("video/webm; codecs=\"vp8, vorbis\""),
            CanPlayTypeResult::kProbably);
  EXPECT_EQ(Ask("video/x-matroska; codecs=\"theora, vorbis\""),
            CanPlayTypeResult::kProbably);
}

TEST_F(CanPlayTypeTest, MatchingIsCaseInsensitiveForTheTypeAndParameterName) {
  EXPECT_EQ(Ask("VIDEO/MP4; CODECS=\"avc1.64001f\""),
            CanPlayTypeResult::kProbably);
  EXPECT_EQ(Ask("  video/mp4 ;  codecs = 'avc1.64001f'"),
            CanPlayTypeResult::kProbably);
}

TEST_F(CanPlayTypeTest, ACodecTheCapabilityQueryRefusesIsNotSupported) {
  const RefusingCapabilities refusing;
  EXPECT_EQ(internal::AnswerCanPlayType("video/mp4; codecs=\"hvc1.1.6.L93.B0\"",
                                        &refusing),
            CanPlayTypeResult::kNone);
}

TEST_F(CanPlayTypeTest, EveryCodecOfTheListIsQueried) {
  const RefusingCapabilities refusing;
  EXPECT_EQ(
      internal::AnswerCanPlayType(
          "video/mp4; codecs=\"avc1.64001f, hvc1.1.6.L93.B0\"", &refusing),
      CanPlayTypeResult::kNone);
  EXPECT_EQ(internal::AnswerCanPlayType(
                "video/mp4; codecs=\"avc1.64001f, mp4a.40.2\"", &refusing),
            CanPlayTypeResult::kProbably);
}

}
}
