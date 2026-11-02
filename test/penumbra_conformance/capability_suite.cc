#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra_conformance/conformance.h"
#include "penumbra_conformance/port_harness.h"

namespace penumbra::conformance {
namespace {

constexpr int kRepeats = 3;
constexpr int kProbeCount = 16;

CapabilityRequest RequestFor(const StreamConfig& config) {
  CapabilityRequest request;
  request.stream = config.info;
  request.mime_type =
      config.info.type() == StreamType::kVideo
          ? "video/mp4; codecs=\"" + config.info.codec_string + "\""
          : "audio/mp4; codecs=\"" + config.info.codec_string + "\"";
  return request;
}

bool SameCapability(const DecodingCapability& a, const DecodingCapability& b) {
  return a.supported == b.supported && a.smooth == b.smooth &&
         a.power_efficient == b.power_efficient &&
         a.preferred_output_mode == b.preferred_output_mode &&
         a.accepts_fragments == b.accepts_fragments;
}

}

TEST_P(CapabilityConformance, UnknownSupportIsFalseNotAnError) {
  auto unknown = VideoStream();
  unknown.info.codec = CodecId::kUnknown;
  unknown.info.codec_string = "unsupported.codec";
  absl::StatusOr<DecodingCapability> capability;
  Run([&] {
    capability = Harness().UnderTest().QueryDecoding(RequestFor(unknown));
  });
  ASSERT_TRUE(capability.ok()) << capability.status();
  EXPECT_FALSE(capability->supported);
}

TEST_P(CapabilityConformance, RepeatedQueriesAreConsistent) {
  for (const auto& config : {VideoStream(), AudioStream()}) {
    absl::StatusOr<DecodingCapability> first;
    Run([&] {
      first = Harness().UnderTest().QueryDecoding(RequestFor(config));
    });
    ASSERT_TRUE(first.ok()) << first.status();
    for (int i = 0; i < kRepeats; ++i) {
      absl::StatusOr<DecodingCapability> again;
      Run([&] {
        again = Harness().UnderTest().QueryDecoding(RequestFor(config));
      });
      ASSERT_TRUE(again.ok()) << again.status();
      EXPECT_TRUE(SameCapability(*first, *again));
    }
  }
}

TEST_P(CapabilityConformance, SmoothAndPowerEfficientImplySupported) {
  for (const auto& config : {VideoStream(), AudioStream()}) {
    absl::StatusOr<DecodingCapability> capability;
    Run([&] {
      capability = Harness().UnderTest().QueryDecoding(RequestFor(config));
    });
    ASSERT_TRUE(capability.ok()) << capability.status();
    if (!capability->supported) {
      EXPECT_FALSE(capability->smooth);
      EXPECT_FALSE(capability->power_efficient);
      EXPECT_FALSE(capability->accepts_fragments);
    }
  }
}

TEST_P(CapabilityConformance, QueriesDoNotConsumePlayerInstances) {
  Run([this] {
    for (int i = 0; i < kProbeCount; ++i) {
      EXPECT_TRUE(
          Harness().UnderTest().QueryDecoding(RequestFor(VideoStream())).ok());
      EXPECT_TRUE(Harness()
                      .UnderTest()
                      .CanChangeType(VideoStream(), VideoStream())
                      .ok());
    }
  });
  EXPECT_TRUE(Create().ok());
}

TEST_P(CapabilityConformance, SupportedStreamsCanCreateAPlayer) {
  absl::StatusOr<DecodingCapability> video;
  absl::StatusOr<DecodingCapability> audio;
  Run([&] {
    video = Harness().UnderTest().QueryDecoding(RequestFor(VideoStream()));
    audio = Harness().UnderTest().QueryDecoding(RequestFor(AudioStream()));
  });
  ASSERT_TRUE(video.ok()) << video.status();
  ASSERT_TRUE(audio.ok()) << audio.status();
  if (!video->supported || !audio->supported) {
    GTEST_SKIP() << "port does not support the synthetic clip codecs";
  }
  EXPECT_TRUE(Create().ok());
}

TEST_P(CapabilityConformance, ChangeAcrossStreamTypesIsFalseNotAnError) {
  absl::StatusOr<bool> result;
  Run([&] {
    result = Harness().UnderTest().CanChangeType(VideoStream(), AudioStream());
  });
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_FALSE(*result);
}

TEST_P(CapabilityConformance, ChangeTypeAnswersAreStable) {
  auto target = VideoStream();
  target.info.codec = CodecId::kHevc;
  target.info.codec_string = "hvc1.1.6.L93.B0";
  absl::StatusOr<bool> first;
  absl::StatusOr<bool> second;
  Run([&] {
    first = Harness().UnderTest().CanChangeType(VideoStream(), target);
    second = Harness().UnderTest().CanChangeType(VideoStream(), target);
  });
  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(*first, *second);
}

TEST_P(CapabilityConformance, ChangeTypeAgreesWithConfigChangeWrites) {
  ALUMBRA_PORT_REQUIRE(Capability::kDataRequestHook);
  auto target = VideoStream();
  target.info.codec = CodecId::kHevc;
  target.info.codec_string = "hvc1.1.6.L93.B0";
  absl::StatusOr<bool> allowed;
  Run([&] {
    allowed = Harness().UnderTest().CanChangeType(VideoStream(), target);
  });
  ASSERT_TRUE(allowed.ok()) << allowed.status();
  ASSERT_TRUE(Create().ok());
  ASSERT_TRUE(Grant(kVideoTrack));
  auto changed = MakeSample(kVideoTrack, 0);
  changed.config_change = std::make_shared<StreamInfo>(target.info);
  const auto plain = MakeSample(kVideoTrack, 1);
  Run([&] {
    const auto status =
        CurrentPlayer().WriteSamples(Ticket(), kVideoTrack, {&changed, 1});
    if (*allowed) {
      EXPECT_TRUE(status.ok()) << status;
    } else {
      EXPECT_EQ(status.code(), absl::StatusCode::kUnimplemented);
      EXPECT_TRUE(CurrentPlayer()
                      .WriteSamples(Ticket(), kVideoTrack, {&plain, 1})
                      .ok());
    }
  });
}

}
