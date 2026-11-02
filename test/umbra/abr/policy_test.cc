#include "umbra/abr/policy.h"

#include <cstdint>

#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "umbra/base/test_support/fake_tick_clock.h"

namespace umbra::abr {
namespace {

AbrSnapshot Snapshot() {
  return {.bandwidth_estimate_bps = 1000000,
          .buffer_ms = 20000,
          .segment_duration_ms = 4000,
          .current_rendition_id = 2,
          .candidates = {{.id = 1,
                          .bitrate_bps = 300000,
                          .height = 360,
                          .frame_rate_numerator = 30,
                          .frame_rate_denominator = 1,
                          .score = 0.5},
                         {.id = 2,
                          .bitrate_bps = 700000,
                          .height = 720,
                          .frame_rate_numerator = 30,
                          .frame_rate_denominator = 1,
                          .score = 0.8},
                         {.id = 3,
                          .bitrate_bps = 1500000,
                          .height = 1080,
                          .frame_rate_numerator = 60,
                          .frame_rate_denominator = 1,
                          .score = 1.0}},
          .max_height = 0,
          .max_bitrate_bps = 0,
          .cmsd_max_bitrate_bps = 0,
          .decode_drop_max_height = 0,
          .force_frame_rate_change = false};
}

TEST(AbrPolicyTest, DisabledPolicyHoldsSelectedRendition) {
  AbrPolicyConfig config;
  config.enabled = false;
  const auto decision = CreateAbrPolicy(config)->Decide(Snapshot());
  EXPECT_EQ(decision.rendition_id, 2U);
  EXPECT_EQ(decision.reason, AbrReason::kDisabled);
  EXPECT_FALSE(decision.abandon_in_flight_segment);
}

TEST(AbrPolicyTest, StartupUsesConservativeBandwidthFraction) {
  AbrSnapshot snapshot = Snapshot();
  snapshot.current_rendition_id = 99;
  snapshot.bandwidth_estimate_bps = 1000000;
  const auto decision = CreateAbrPolicy()->Decide(snapshot);
  EXPECT_EQ(decision.rendition_id, 2U);
  EXPECT_EQ(decision.reason, AbrReason::kStartup);
}

TEST(AbrPolicyTest, CapsApplyBeforeTheHybridRule) {
  AbrSnapshot snapshot = Snapshot();
  snapshot.max_height = 720;
  snapshot.max_bitrate_bps = 800000;
  const auto decision = CreateAbrPolicy()->Decide(snapshot);
  EXPECT_NE(decision.rendition_id, 3U);
  EXPECT_EQ(decision.reason, AbrReason::kStable);
}

TEST(AbrPolicyTest, DoesNotChangeFrameRateWithoutAnExplicitForce) {
  AbrSnapshot snapshot = Snapshot();
  snapshot.current_rendition_id = 3;
  snapshot.bandwidth_estimate_bps = 200000;
  const auto decision = CreateAbrPolicy()->Decide(snapshot);
  EXPECT_EQ(decision.rendition_id, 3U);
  EXPECT_EQ(decision.reason, AbrReason::kStable);
}

TEST(AbrPolicyTest, LowBufferUsesThroughputRule) {
  AbrSnapshot snapshot = Snapshot();
  snapshot.buffer_ms = 1000;
  snapshot.bandwidth_estimate_bps = 400000;
  AbrPolicyConfig config;
  config.emergency_down_switch = false;
  const auto decision = CreateAbrPolicy(config)->Decide(snapshot);
  EXPECT_EQ(decision.rendition_id, 1U);
  EXPECT_EQ(decision.reason, AbrReason::kThroughput);
}

TEST(AbrPolicyTest, HealthyBufferUsesBufferRule) {
  AbrSnapshot snapshot = Snapshot();
  snapshot.current_rendition_id = 1;
  snapshot.buffer_ms = 30000;
  const auto decision = CreateAbrPolicy()->Decide(snapshot);
  EXPECT_EQ(decision.rendition_id, 2U);
  EXPECT_EQ(decision.reason, AbrReason::kBuffer);
}

TEST(AbrPolicyTest, SelectsAudioBitratesFromTheProvidedGroup) {
  AbrSnapshot snapshot = Snapshot();
  snapshot.current_rendition_id = 2;
  snapshot.buffer_ms = 1000;
  snapshot.bandwidth_estimate_bps = 100000;
  snapshot.candidates = {{.id = 1,
                          .bitrate_bps = 64000,
                          .height = 0,
                          .frame_rate_numerator = 0,
                          .frame_rate_denominator = 1,
                          .score = 0.0},
                         {.id = 2,
                          .bitrate_bps = 128000,
                          .height = 0,
                          .frame_rate_numerator = 0,
                          .frame_rate_denominator = 1,
                          .score = 0.0}};
  const auto decision = CreateAbrPolicy()->Decide(snapshot);
  EXPECT_EQ(decision.rendition_id, 1U);
  EXPECT_EQ(decision.reason, AbrReason::kEmergencyDownSwitch);
}

TEST(AbrPolicyTest, HlsScoreRanksCandidatesAfterTheOtherRulesTie) {
  AbrSnapshot snapshot = Snapshot();
  snapshot.current_rendition_id = 1;
  snapshot.buffer_ms = 30000;
  snapshot.candidates = {{.id = 1,
                          .bitrate_bps = 500000,
                          .height = 480,
                          .frame_rate_numerator = 30,
                          .frame_rate_denominator = 1,
                          .score = 0.4},
                         {.id = 2,
                          .bitrate_bps = 500000,
                          .height = 480,
                          .frame_rate_numerator = 30,
                          .frame_rate_denominator = 1,
                          .score = 0.9}};
  const auto decision = CreateAbrPolicy()->Decide(snapshot);
  EXPECT_EQ(decision.rendition_id, 2U);
  EXPECT_EQ(decision.reason, AbrReason::kBuffer);
}

TEST(AbrPolicyTest, FakeClockTriggersEmergencyAbandonmentBeforeBufferDrains) {
  base::FakeTickClock clock;
  AbrSnapshot snapshot = Snapshot();
  snapshot.bandwidth_estimate_bps = 500000;
  snapshot.buffer_ms = 3000;
  snapshot.segment_duration_ms = 4000;
  snapshot.now_ms = absl::ToInt64Milliseconds(clock.NowTicks().SinceOrigin());
  const auto policy = CreateAbrPolicy();
  const auto decision = policy->Decide(snapshot);
  EXPECT_EQ(decision.rendition_id, 1U);
  EXPECT_EQ(decision.reason, AbrReason::kEmergencyDownSwitch);
  EXPECT_TRUE(decision.abandon_in_flight_segment);
  clock.Advance(absl::Seconds(2));
  snapshot.now_ms = absl::ToInt64Milliseconds(clock.NowTicks().SinceOrigin());
  snapshot.buffer_ms = 1000;
  const auto before_drain_decision = policy->Decide(snapshot);
  EXPECT_LT(snapshot.now_ms, 3000);
  EXPECT_EQ(before_drain_decision.reason, AbrReason::kEmergencyDownSwitch);
  EXPECT_TRUE(before_drain_decision.abandon_in_flight_segment);
}

TEST(AbrPolicyTest, SwitchHistoryBoundsBoundaryOscillation) {
  AbrPolicyConfig config;
  config.minimum_buffer_for_up_switch_ms = 0;
  const auto policy = CreateAbrPolicy(config);
  std::uint64_t current_id = 1;
  std::int64_t last_switch_ms = -1;
  std::int32_t switches = 0;
  for (std::int64_t step = 0; step < 100; ++step) {
    AbrSnapshot snapshot = Snapshot();
    snapshot.current_rendition_id = current_id;
    snapshot.buffer_ms = 1000;
    snapshot.segment_duration_ms = 0;
    snapshot.bandwidth_estimate_bps = step % 2 == 0 ? 850000 : 800000;
    snapshot.now_ms = step * 100;
    snapshot.last_switch_ms = last_switch_ms;
    const auto decision = policy->Decide(snapshot);
    if (decision.rendition_id != current_id) {
      ++switches;
      current_id = decision.rendition_id;
      last_switch_ms = snapshot.now_ms;
    }
  }
  EXPECT_GT(switches, 0);
  EXPECT_LE(switches, 4);
}

TEST(AbrPolicyTest, ReasonCodeHasStableText) {
  EXPECT_EQ(ToString(AbrReason::kEmergencyDownSwitch), "emergency_down_switch");
}

}
}
