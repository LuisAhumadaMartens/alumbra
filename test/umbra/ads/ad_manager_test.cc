#include "umbra/ads/ad_manager.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"
#include "umbra/ads/manifest_bridge.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/manifest/dash_mpd_parser.h"
#include "umbra/manifest/presentation.h"

namespace umbra::ads {
namespace {

class FakeContent final : public ContentController {
 public:
  [[nodiscard]] ContentSnapshot Snapshot() const override {
    return {.position = position, .selected_tracks = tracks};
  }
  void BeginContent() override { ++begins; }
  void PauseContent() override { ++pauses; }
  void SeekContent(penumbra::MediaTime target) override { position = target; }
  void ResumeContent(const ContentSnapshot& snapshot) override {
    position = snapshot.position;
    tracks = snapshot.selected_tracks;
    restored.push_back(snapshot);
  }
  void DispatchEnded() override { ++ended; }

  penumbra::MediaTime position = penumbra::MediaTime::FromSeconds(3);
  std::vector<std::string> tracks = {"audio-en", "video-main"};
  std::vector<ContentSnapshot> restored;
  int begins = 0;
  int pauses = 0;
  int ended = 0;
};

class FakeProvider final : public AdProvider {
 public:
  ProviderResult Prepare(const AdBreak& ad_break) override {
    prepared.push_back(ad_break.id);
    return fail_stage == ProviderStage::kPrepare
               ? ProviderResult{.ok = false,
                                .provider_code = "prepare",
                                .message = "prepare failed"}
               : ProviderResult{};
  }
  ProviderResult Start(const Ad& ad) override {
    started.push_back(ad.id);
    return fail_stage == ProviderStage::kStart
               ? ProviderResult{.ok = false,
                                .provider_code = "start",
                                .message = "start failed"}
               : ProviderResult{};
  }
  ProviderProgress Advance(penumbra::MediaTime now) override {
    static_cast<void>(now);
    if (fail_stage == ProviderStage::kAdvance) {
      return {.ok = false,
              .complete = false,
              .provider_code = "advance",
              .message = "advance failed"};
    }
    return ProviderProgress{.ok = true,
                            .complete = complete_on_advance,
                            .provider_code = {},
                            .message = {}};
  }

  ProviderStage fail_stage = ProviderStage::kTimeout;
  bool complete_on_advance = true;
  std::vector<std::string> prepared;
  std::vector<std::string> started;
};

AdBreak Break(std::string id, AdOffset offset) {
  AdBreak ad_break;
  ad_break.id = std::move(id);
  ad_break.offset = offset;
  ad_break.pod.ads.push_back({.id = ad_break.id + "-ad",
                              .duration = penumbra::TimeDelta::FromSeconds(2),
                              .skippable = false,
                              .skip_offset = penumbra::TimeDelta::Unknown(),
                              .played = false});
  return ad_break;
}

class AdManagerTest : public testing::Test {
 protected:
  void SetUp() override { ResetManager(); }

  void ResetManager() {
    content = FakeContent();
    provider = FakeProvider();
    errors.clear();
    manager = std::make_unique<AdManager>(
        options, content, provider,
        [this](const AdErrorEvent& event) { errors.push_back(event); });
  }

  void SetBreaks(std::vector<AdBreak> breaks) {
    ASSERT_TRUE(
        manager
            ->SetBreaks(std::move(breaks), penumbra::TimeDelta::FromSeconds(10))
            .ok);
  }

  void CompleteActive(
      penumbra::MediaTime now = penumbra::MediaTime::FromSeconds(1)) {
    manager->Tick(now);
  }

  AdManagerOptions options;
  FakeContent content;
  FakeProvider provider;
  std::vector<AdErrorEvent> errors;
  std::unique_ptr<AdManager> manager;
};

TEST(AdOffsetTest, ResolvesStartEndAbsoluteAndPercentageOffsets) {
  const penumbra::TimeDelta duration = penumbra::TimeDelta::FromSeconds(10);
  const std::vector<std::pair<AdOffset, int64_t>> cases = {
      {{.kind = OffsetKind::kStart, .value = 0.0}, 0},
      {{.kind = OffsetKind::kEnd, .value = 0.0}, 10'000'000},
      {{.kind = OffsetKind::kAbsolute, .value = 3.5}, 3'500'000},
      {{.kind = OffsetKind::kPercentage, .value = 25.0}, 2'500'000},
      {{.kind = OffsetKind::kPercentage, .value = 100.0}, 10'000'000},
  };
  for (const auto& [offset, expected] : cases) {
    const auto resolved = ResolveOffset(offset, duration);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->InMicroseconds(), expected);
  }
  EXPECT_FALSE(
      ResolveOffset({.kind = OffsetKind::kPercentage, .value = 101.0}, duration)
          .has_value());
}

TEST(AdOffsetTest, BridgesManifestBreaksWithoutCopyingTheirType) {
  umbra::manifest::AdBreak cue;
  cue.id = "manifest-mid";
  cue.start = penumbra::MediaTime::FromSeconds(8);
  cue.duration = penumbra::TimeDelta::FromSeconds(12);
  const auto converted = FromManifestCue(cue);
  ASSERT_TRUE(converted.has_value());
  EXPECT_EQ(converted->id, "manifest-mid");
  EXPECT_EQ(converted->offset.kind, OffsetKind::kAbsolute);
  EXPECT_DOUBLE_EQ(converted->offset.value, 8.0);
  ASSERT_TRUE(converted->scheduled_duration.has_value());
  EXPECT_EQ(converted->scheduled_duration->InMicroseconds(), 12'000'000);
}

TEST_F(AdManagerTest, PrefetchesAtLeadAndDispatchesBreakTrackingInOrder) {
  std::vector<std::string> calls;
  options.prefetch_lead = penumbra::TimeDelta::FromSeconds(5);
  options.on_prefetch = [&calls](const AdBreak& ad_break) {
    calls.push_back("prefetch:" + ad_break.id);
  };
  options.on_tracking_event = [&calls](std::string_view event,
                                       std::string_view uri,
                                       const AdBreak& ad_break) {
    calls.push_back(ad_break.id + ":" + std::string(event) + ":" +
                    std::string(uri));
  };
  ResetManager();
  AdBreak ad_break = Break("mid", {.kind = OffsetKind::kAbsolute, .value = 10});
  ad_break.sources.push_back({.kind = AdSourceKind::kAdTagUri,
                              .uri = "https://ads.test/mid",
                              .template_type = {},
                              .allow_multiple_ads = false,
                              .follow_redirects = true});
  ad_break.tracking_events = {{"breakStart", "start-uri"},
                              {"breakEnd", "end-uri"}};
  ASSERT_TRUE(
      manager->SetBreaks({ad_break}, penumbra::TimeDelta::FromSeconds(20)).ok);
  manager->StartContent(penumbra::MediaTime::Zero());
  manager->OnContentTime(penumbra::MediaTime::Zero(),
                         penumbra::MediaTime::FromSeconds(4),
                         penumbra::MediaTime::FromSeconds(4));
  EXPECT_TRUE(calls.empty());
  manager->OnContentTime(penumbra::MediaTime::FromSeconds(4),
                         penumbra::MediaTime::FromSeconds(5),
                         penumbra::MediaTime::FromSeconds(5));
  EXPECT_EQ(calls, std::vector<std::string>{"prefetch:mid"});
  manager->OnContentTime(penumbra::MediaTime::FromSeconds(5),
                         penumbra::MediaTime::FromSeconds(10),
                         penumbra::MediaTime::FromSeconds(10));
  ASSERT_EQ(calls.size(), 2U);
  EXPECT_EQ(calls[1], "mid:breakStart:start-uri");
  CompleteActive(penumbra::MediaTime::FromSeconds(11));
  ASSERT_EQ(calls.size(), 3U);
  EXPECT_EQ(calls[2], "mid:breakEnd:end-uri");
}

TEST_F(AdManagerTest, ManifestFixtureBreakFlowsThroughDesktopScheduler) {
  std::ifstream file(std::string(ALUMBRA_ADS_FIXTURE_DIR) + "/live-events.mpd");
  const std::string xml{std::istreambuf_iterator<char>(file),
                        std::istreambuf_iterator<char>()};
  auto presentation = umbra::manifest::ParseDashMpd(xml);
  ASSERT_TRUE(presentation.ok()) << presentation.status();
  ASSERT_EQ(presentation->ad_breaks.size(), 1U);
  std::optional<AdBreak> ad_break =
      FromManifestCue(presentation->ad_breaks.front());
  ASSERT_TRUE(ad_break.has_value());
  ad_break->pod.ads.push_back({.id = "fixture-ad",
                               .duration = penumbra::TimeDelta::FromSeconds(1),
                               .skippable = false,
                               .skip_offset = penumbra::TimeDelta::Unknown(),
                               .played = false});
  const int64_t cue_time_us =
      presentation->ad_breaks.front().start.InMicroseconds();
  const penumbra::MediaTime cue_time =
      penumbra::MediaTime::FromMicroseconds(cue_time_us);
  ASSERT_TRUE(manager
                  ->SetBreaks({std::move(*ad_break)},
                              penumbra::TimeDelta::FromMicroseconds(
                                  cue_time_us + 10'000'000))
                  .ok);
  const std::vector<penumbra::MediaTime> cue_points = manager->CuePoints();
  ASSERT_EQ(cue_points.size(), 1U);
  EXPECT_EQ(cue_points.front(), cue_time);
  manager->StartContent(penumbra::MediaTime::Zero());
  manager->OnContentTime(cue_time - penumbra::TimeDelta::FromSeconds(1),
                         cue_time, cue_time);
  ASSERT_TRUE(manager->InAdBreak());
  ASSERT_EQ(provider.started.size(), 1U);
  EXPECT_EQ(provider.started.front(), "fixture-ad");
  manager->Tick(penumbra::MediaTime::FromSeconds(3));
  ASSERT_EQ(content.restored.size(), 1U);
  EXPECT_EQ(content.position.InMicroseconds(), 3'000'000);
}

TEST_F(AdManagerTest, TableDrivenRollsTriggerAndRestoreContent) {
  enum class Trigger { kPreRoll, kMidRoll, kPostRoll };
  struct RollCase {
    std::string id;
    AdOffset offset;
    Trigger trigger;
  };
  const std::vector<RollCase> cases = {
      {"pre", {.kind = OffsetKind::kStart, .value = 0.0}, Trigger::kPreRoll},
      {"mid",
       {.kind = OffsetKind::kPercentage, .value = 50.0},
       Trigger::kMidRoll},
      {"post", {.kind = OffsetKind::kEnd, .value = 0.0}, Trigger::kPostRoll},
  };
  for (const RollCase& test_case : cases) {
    ResetManager();
    SetBreaks({Break(test_case.id, test_case.offset)});
    manager->StartContent(penumbra::MediaTime::Zero());
    if (test_case.trigger == Trigger::kMidRoll) {
      manager->OnContentTime(penumbra::MediaTime::FromSeconds(4),
                             penumbra::MediaTime::FromSeconds(5),
                             penumbra::MediaTime::FromSeconds(5));
    } else if (test_case.trigger == Trigger::kPostRoll) {
      manager->ContentEnded(penumbra::MediaTime::FromSeconds(10));
      EXPECT_TRUE(manager->EndedPending());
      EXPECT_EQ(content.ended, 0);
    }
    EXPECT_TRUE(manager->InAdBreak());
    CompleteActive(penumbra::MediaTime::FromSeconds(11));
    EXPECT_FALSE(manager->InAdBreak());
    ASSERT_EQ(content.restored.size(), 1U);
    EXPECT_EQ(content.position.InMicroseconds(), 3'000'000);
    EXPECT_EQ(content.tracks,
              (std::vector<std::string>{"audio-en", "video-main"}));
    EXPECT_EQ(content.begins, 1);
    EXPECT_EQ(content.ended, test_case.trigger == Trigger::kPostRoll ? 1 : 0);
    EXPECT_FALSE(manager->EndedPending());
  }
}

TEST_F(AdManagerTest, EndedDispatchesOnceWhenThereIsNoPostRoll) {
  SetBreaks({Break("mid", {.kind = OffsetKind::kAbsolute, .value = 2.0})});
  manager->StartContent(penumbra::MediaTime::Zero());
  manager->ContentEnded(penumbra::MediaTime::FromSeconds(10));
  EXPECT_FALSE(manager->EndedPending());
  EXPECT_EQ(content.ended, 1);
}

TEST_F(AdManagerTest, SeekPolicySelectsBreaksCrossedBySeek) {
  const std::vector<SeekPolicy> policies = {SeekPolicy::kLast, SeekPolicy::kAll,
                                            SeekPolicy::kNone};
  const std::vector<std::size_t> expected_starts = {1, 3, 0};
  for (std::size_t case_index = 0; case_index < policies.size(); ++case_index) {
    options.seek_policy = policies[case_index];
    ResetManager();
    SetBreaks({Break("one", {.kind = OffsetKind::kAbsolute, .value = 2.0}),
               Break("two", {.kind = OffsetKind::kAbsolute, .value = 4.0}),
               Break("three", {.kind = OffsetKind::kAbsolute, .value = 6.0})});
    manager->StartContent(penumbra::MediaTime::Zero());
    manager->Seek(penumbra::MediaTime::Zero(),
                  penumbra::MediaTime::FromSeconds(8),
                  penumbra::MediaTime::FromSeconds(8));
    for (int index = 0; index < 4 && manager->InAdBreak(); ++index) {
      CompleteActive(penumbra::MediaTime::FromSeconds(8 + index));
    }
    EXPECT_EQ(provider.started.size(), expected_starts[case_index]);
    if (policies[case_index] == SeekPolicy::kLast) {
      ASSERT_EQ(provider.started.size(), 1U);
      EXPECT_EQ(provider.started.front(), "three-ad");
    }
  }
}

TEST_F(AdManagerTest, PlayedBreaksReplayOnlyWhenEnabled) {
  SetBreaks({Break("mid", {.kind = OffsetKind::kAbsolute, .value = 2.0})});
  manager->StartContent(penumbra::MediaTime::Zero());
  manager->OnContentTime(penumbra::MediaTime::FromSeconds(1),
                         penumbra::MediaTime::FromSeconds(2),
                         penumbra::MediaTime::FromSeconds(2));
  CompleteActive(penumbra::MediaTime::FromSeconds(3));
  manager->OnContentTime(penumbra::MediaTime::FromSeconds(1),
                         penumbra::MediaTime::FromSeconds(2),
                         penumbra::MediaTime::FromSeconds(4));
  EXPECT_EQ(provider.started.size(), 1U);

  options.replay_played_breaks = true;
  ResetManager();
  SetBreaks({Break("mid", {.kind = OffsetKind::kAbsolute, .value = 2.0})});
  manager->StartContent(penumbra::MediaTime::Zero());
  manager->OnContentTime(penumbra::MediaTime::FromSeconds(1),
                         penumbra::MediaTime::FromSeconds(2),
                         penumbra::MediaTime::FromSeconds(2));
  CompleteActive(penumbra::MediaTime::FromSeconds(3));
  manager->OnContentTime(penumbra::MediaTime::FromSeconds(1),
                         penumbra::MediaTime::FromSeconds(2),
                         penumbra::MediaTime::FromSeconds(4));
  EXPECT_EQ(provider.started.size(), 2U);

  ResetManager();
  SetBreaks({Break("post", {.kind = OffsetKind::kEnd, .value = 0.0})});
  manager->StartContent(penumbra::MediaTime::Zero());
  manager->ContentEnded(penumbra::MediaTime::FromSeconds(10));
  CompleteActive(penumbra::MediaTime::FromSeconds(11));
  EXPECT_EQ(content.ended, 1);
  EXPECT_FALSE(manager->InAdBreak());
  manager->ContentEnded(penumbra::MediaTime::FromSeconds(12));
  EXPECT_EQ(content.ended, 1);
}

TEST_F(AdManagerTest, ProviderFailuresAndTimeoutRestoreContentAndEmitAdErrors) {
  const std::vector<std::pair<ProviderStage, penumbra::MediaTime>> cases = {
      {ProviderStage::kPrepare, penumbra::MediaTime::FromSeconds(1)},
      {ProviderStage::kStart, penumbra::MediaTime::FromSeconds(1)},
      {ProviderStage::kAdvance, penumbra::MediaTime::FromSeconds(1)},
      {ProviderStage::kTimeout, penumbra::MediaTime::FromSeconds(32)},
  };
  for (const auto& [stage, now] : cases) {
    options.seek_policy = SeekPolicy::kLast;
    ResetManager();
    provider.fail_stage = stage;
    provider.complete_on_advance = false;
    SetBreaks({Break("mid", {.kind = OffsetKind::kAbsolute, .value = 2.0})});
    manager->StartContent(penumbra::MediaTime::Zero());
    manager->OnContentTime(penumbra::MediaTime::FromSeconds(1),
                           penumbra::MediaTime::FromSeconds(2),
                           penumbra::MediaTime::FromSeconds(1));
    if (manager->InAdBreak()) {
      manager->Tick(now);
    }
    EXPECT_FALSE(manager->InAdBreak());
    EXPECT_EQ(content.restored.size(), 1U);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors.front().break_id, "mid");
    EXPECT_EQ(errors.front().code, umbra::base::ErrorCode::kAdsUnavailable);
  }
}

TEST_F(AdManagerTest, EmptyPodCompletesWithoutPausingContent) {
  AdBreak empty;
  empty.id = "empty";
  empty.offset = {.kind = OffsetKind::kStart, .value = 0.0};
  SetBreaks({empty});
  manager->StartContent(penumbra::MediaTime::Zero());
  EXPECT_FALSE(manager->InAdBreak());
  EXPECT_EQ(content.pauses, 0);
  EXPECT_TRUE(errors.empty());
}

}
}
