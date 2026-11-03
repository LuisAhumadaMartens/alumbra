#include "umbra/ads/provider.h"

#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"
#include "umbra/ads/ad_manager.h"

namespace umbra::ads {
namespace {

Ad MakeAd(std::string id) {
  return {.id = std::move(id),
          .duration = penumbra::TimeDelta::FromSeconds(10),
          .skippable = true,
          .skip_offset = penumbra::TimeDelta::FromSeconds(5),
          .played = false};
}

class ContractProvider final : public ProviderLifecycle {
 public:
  [[nodiscard]] ProviderCapabilities Capabilities() const override {
    return {.renders_own_ui = false,
            .owns_skip_control = false,
            .fires_own_beacons = false,
            .supports_preloading = true,
            .needs_content_video_element = false};
  }
  ProviderResult RequestAds(const AdRequest& request) override {
    received_consent = request.consent;
    calls.emplace_back("request");
    return {};
  }
  ProviderResult StartAd(const Ad& ad) override {
    calls.push_back("start:" + ad.id);
    return {};
  }
  ProviderResult PauseAd() override {
    calls.emplace_back("pause");
    return {};
  }
  ProviderResult ResumeAd() override {
    calls.emplace_back("resume");
    return {};
  }
  ProviderResult SkipAd() override {
    calls.emplace_back("skip");
    return {};
  }
  ProviderResult Resize(AdSize size) override {
    calls.push_back("resize:" + std::to_string(size.width) + "x" +
                    std::to_string(size.height));
    return {};
  }
  void ContentProgress(penumbra::MediaTime position) override {
    static_cast<void>(position);
    calls.emplace_back("progress");
  }
  void ContentComplete() override { calls.emplace_back("content_complete"); }
  void SetEventCallback(AdProviderEventCallback callback) override {
    callback_ = std::move(callback);
  }
  void Destroy() override { calls.emplace_back("destroy"); }

  AdConsent received_consent;
  std::vector<std::string> calls;

 private:
  AdProviderEventCallback callback_;
};

void ExerciseContract(ProviderLifecycle& provider,
                      const ProviderCapabilities& expected_capabilities) {
  EXPECT_EQ(provider.Capabilities().renders_own_ui,
            expected_capabilities.renders_own_ui);
  EXPECT_EQ(provider.Capabilities().owns_skip_control,
            expected_capabilities.owns_skip_control);
  EXPECT_EQ(provider.Capabilities().fires_own_beacons,
            expected_capabilities.fires_own_beacons);
  EXPECT_EQ(provider.Capabilities().supports_preloading,
            expected_capabilities.supports_preloading);
  EXPECT_EQ(provider.Capabilities().needs_content_video_element,
            expected_capabilities.needs_content_video_element);
  EXPECT_TRUE(provider
                  .RequestAds({.ad_tag_url = "https://ads.test/tag",
                               .consent = {.tcf_string = "tcf-raw",
                                           .gpp_string = "gpp-raw",
                                           .limited_ads_enabled = true,
                                           .child_directed_enabled = false}})
                  .ok);
  EXPECT_TRUE(provider.StartAd(MakeAd("creative")).ok);
  EXPECT_TRUE(provider.PauseAd().ok);
  EXPECT_TRUE(provider.ResumeAd().ok);
  EXPECT_TRUE(provider.SkipAd().ok);
  EXPECT_TRUE(provider.Resize({.width = 640, .height = 360}).ok);
  provider.ContentProgress(penumbra::MediaTime::FromSeconds(1));
  provider.ContentComplete();
  provider.Destroy();
}

TEST(AdProviderContractTest, MockProviderImplementsEveryOperation) {
  ContractProvider provider;
  ExerciseContract(provider, provider.Capabilities());
  EXPECT_EQ(provider.calls,
            (std::vector<std::string>{
                "request", "start:creative", "pause", "resume", "skip",
                "resize:640x360", "progress", "content_complete", "destroy"}));
  EXPECT_EQ(provider.received_consent.tcf_string, "tcf-raw");
  EXPECT_EQ(provider.received_consent.gpp_string, "gpp-raw");
  EXPECT_TRUE(provider.received_consent.limited_ads_enabled);
}

TEST(AdProviderContractTest, BuiltInProviderImplementsEveryOperation) {
  BuiltInAdProvider provider(
      [](const AdRequest& request, std::vector<AdBreak>& breaks) {
        EXPECT_EQ(request.consent.tcf_string, "tcf-raw");
        AdBreak ad_break;
        ad_break.id = "break";
        breaks.push_back(std::move(ad_break));
        return ProviderResult{};
      });
  std::vector<AdProviderEvent::Type> events;
  provider.SetEventCallback([&events](const AdProviderEvent& event) {
    events.emplace_back(event.type);
  });
  ExerciseContract(provider, {.renders_own_ui = false,
                              .owns_skip_control = false,
                              .fires_own_beacons = false,
                              .supports_preloading = true,
                              .needs_content_video_element = false});
  EXPECT_EQ(provider.Breaks().size(), 0U);
}

TEST(AdProviderEventTest, BuiltInProviderEmitsLifecycleInCallOrder) {
  BuiltInAdProvider provider(
      [](const AdRequest&, std::vector<AdBreak>&) { return ProviderResult{}; });
  std::vector<AdProviderEvent::Type> events;
  provider.SetEventCallback([&events](const AdProviderEvent& event) {
    events.emplace_back(event.type);
  });
  EXPECT_TRUE(provider.RequestAds({}).ok);
  EXPECT_TRUE(provider.StartAd(MakeAd("ad")).ok);
  provider.SetSkippable(false);
  provider.ReportProgress(penumbra::TimeDelta::FromMicroseconds(2'500'000));
  provider.ReportProgress(penumbra::TimeDelta::FromMicroseconds(5'000'000));
  provider.ReportProgress(penumbra::TimeDelta::FromMicroseconds(7'500'000));
  provider.ReportClick();
  EXPECT_TRUE(provider.PauseAd().ok);
  EXPECT_TRUE(provider.ResumeAd().ok);
  provider.ReportMuted(true);
  provider.ReportMuted(false);
  provider.CompleteAd();
  provider.EndBreak();
  provider.ContentComplete();
  EXPECT_EQ(
      events,
      (std::vector<AdProviderEvent::Type>{
          AdProviderEvent::Type::kAdsLoaded,
          AdProviderEvent::Type::kAdBreakStart, AdProviderEvent::Type::kAdStart,
          AdProviderEvent::Type::kAdSkippableStateChange,
          AdProviderEvent::Type::kAdSkippableStateChange,
          AdProviderEvent::Type::kAdProgress,
          AdProviderEvent::Type::kAdQuartile,
          AdProviderEvent::Type::kAdProgress,
          AdProviderEvent::Type::kAdQuartile,
          AdProviderEvent::Type::kAdProgress,
          AdProviderEvent::Type::kAdQuartile, AdProviderEvent::Type::kAdClick,
          AdProviderEvent::Type::kAdPause, AdProviderEvent::Type::kAdResume,
          AdProviderEvent::Type::kAdMute, AdProviderEvent::Type::kAdUnmute,
          AdProviderEvent::Type::kAdComplete,
          AdProviderEvent::Type::kAdBreakEnd,
          AdProviderEvent::Type::kAllAdsCompleted}));
}

TEST(AdProviderEventTest, SkipAndFailureEmitAdSpecificTerminationEvents) {
  BuiltInAdProvider provider(
      [](const AdRequest&, std::vector<AdBreak>&) { return ProviderResult{}; });
  std::vector<AdProviderEvent::Type> events;
  provider.SetEventCallback([&events](const AdProviderEvent& event) {
    events.emplace_back(event.type);
  });
  EXPECT_TRUE(provider.StartAd(MakeAd("skipped")).ok);
  EXPECT_TRUE(provider.SkipAd().ok);
  provider.EndBreak();
  provider.ReportError("vast-403");
  EXPECT_EQ(
      events,
      (std::vector<AdProviderEvent::Type>{
          AdProviderEvent::Type::kAdBreakStart, AdProviderEvent::Type::kAdStart,
          AdProviderEvent::Type::kAdSkippableStateChange,
          AdProviderEvent::Type::kAdSkip, AdProviderEvent::Type::kAdBreakEnd,
          AdProviderEvent::Type::kAdError}));
}

}
}
