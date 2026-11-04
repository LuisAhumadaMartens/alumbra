#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "fake_port.h"
#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "penumbra/interface/task_runner.h"
#include "umbra/c_api/testing.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/player/player.h"
#include "umbra/player/player_access.h"
#include "umbra/player/text_track.h"
#include "umbra/player/text_track_routing.h"

extern "C" int RunCConsumerBegin(void);
extern "C" int RunCConsumerFinish(void);

namespace {

class OptionsRunner final : public penumbra::ControlTaskRunner {
 public:
  explicit OptionsRunner(umbra::player::PlayerOptions& options)
      : post_task_(options.post_task),
        runs_on_sequence_(options.runs_on_sequence) {}

  absl::Status PostTask(Task task, absl::Duration delay) override {
    auto task_ptr = std::make_shared<Task>(std::move(task));
    post_task_([task_ptr] { std::move (*task_ptr)(); },
               absl::ToChronoMicroseconds(delay));
    return absl::OkStatus();
  }

  [[nodiscard]] bool RunsTasksInCurrentSequence() const override {
    return !runs_on_sequence_ || runs_on_sequence_();
  }

 private:
  std::function<void(std::function<void()>, std::chrono::microseconds)>
      post_task_;
  std::function<bool()> runs_on_sequence_;
};

class FakeSource final : public umbra::pipeline::SampleSource {
 public:
  void Open(std::shared_ptr<umbra::pipeline::SampleSink> sink) override {
    umbra::pipeline::SourceDescription description;
    description.duration = penumbra::MediaTime::FromMilliseconds(3000);
    penumbra::StreamConfig stream;
    stream.info.track_id = 7;
    stream.info.codec = penumbra::CodecId::kH264;
    stream.info.codec_string = "avc1.64001f";
    stream.info.time_base = penumbra::Rational::Of(1, 1000);
    penumbra::VideoInfo video;
    video.coded_width = 640;
    video.coded_height = 360;
    video.display_width = 640;
    video.display_height = 360;
    stream.info.format = video;
    description.streams.push_back(std::move(stream));
    sink->OnOpened(std::move(description));
  }

  void RequestSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                      size_t max_samples) override {
    static_cast<void>(ticket);
    static_cast<void>(track_id);
    static_cast<void>(max_samples);
  }
  [[nodiscard]] umbra::pipeline::SeekPoint ResolveSeek(
      penumbra::MediaTime position,
      [[maybe_unused]] umbra::pipeline::SeekMode mode,
      [[maybe_unused]] umbra::pipeline::FastSeekSnap snap) const override {
    return {.presentation_time = position, .decode_time = position};
  }
  void Seek(penumbra::SeekTicket ticket,
            penumbra::MediaTime position) override {
    static_cast<void>(ticket);
    static_cast<void>(position);
  }
  void Close() override {}
};

std::shared_ptr<penumbra::PlayerFactory> BorrowFactory(
    penumbra::test::FakePort& port) {
  return {std::shared_ptr<void>(),
          static_cast<penumbra::PlayerFactory*>(&port)};
}

std::shared_ptr<penumbra::CapabilityProvider> BorrowCapabilities(
    penumbra::test::FakePort& port) {
  return {std::shared_ptr<void>(),
          static_cast<penumbra::CapabilityProvider*>(&port)};
}

TEST(CApiConsumerTest, DrivesFakePortAndReadsSettingsAndErrors) {
  std::shared_ptr<penumbra::test::FakePort> port;
  umbra::c_api::testing::SetPlayerCreator(
      [&port](umbra::player::PlayerOptions options) {
        auto control = std::make_shared<OptionsRunner>(options);
        port = std::make_shared<penumbra::test::FakePort>(control);
        umbra::player::internal::PlayerDependencies dependencies;
        dependencies.factory = BorrowFactory(*port);
        dependencies.capabilities = BorrowCapabilities(*port);
        dependencies.resolve_source =
            [](std::string_view, umbra::player::TextTrackList*,
               umbra::player::internal::TextTrackRoutingOptions,
               const std::vector<std::string>&,
               bool) -> std::unique_ptr<umbra::pipeline::SampleSource> {
          return std::make_unique<FakeSource>();
        };
        return umbra::player::internal::PlayerAccess::Create(
            std::move(options), control, std::move(dependencies));
      });

  EXPECT_EQ(RunCConsumerBegin(), 0);
  ASSERT_NE(port, nullptr);
  ASSERT_GE(port->PlayersCreated(), 1U);
  ASSERT_NE(port->LatestPlayer(), nullptr);
  ASSERT_FALSE(port->LatestPlayer()->Seeks().empty());
  ASSERT_TRUE(
      port->LatestPlayer()
          ->ReportError({.status = absl::InternalError("fake port failure"),
                         .category = penumbra::PortErrorCategory::kPlatform,
                         .recovery = penumbra::RecoveryHint::kFatal})
          .ok());
  EXPECT_EQ(RunCConsumerFinish(), 0);
  EXPECT_EQ(port->ActivePlayers(), 0U);
  umbra::c_api::testing::SetPlayerCreator({});
}

}
