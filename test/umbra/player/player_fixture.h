#ifndef TEST_UMBRA_PLAYER_PLAYER_FIXTURE_H_
#define TEST_UMBRA_PLAYER_PLAYER_FIXTURE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fake_port.h"
#include "fake_sample_source.h"
#include "gtest/gtest.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/task_runner.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/error_detail.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/player/play_result.h"
#include "umbra/player/player.h"
#include "umbra/settings/platform.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"

namespace umbra::player::test {

inline constexpr uint32_t kVideoTrack = 7;
inline constexpr uint32_t kAudioTrack = 8;

base::ErrorDetail DetailFor(base::ErrorCode code);

class PlayerFixture : public ::testing::Test {
 protected:
  PlayerFixture();

  void TearDown() override;

  void Do(std::function<void()> task);
  void Create();
  std::unique_ptr<Player> CreateWithSettings(settings::Store& settings);
  void SetSrc(const std::string& url, int64_t duration_ms = 3000);
  void Open();
  void Grant(uint32_t track, size_t count = 3);
  void PortState(penumbra::PlayerState state);
  void Preroll(size_t batches = 2);
  void Destroy();
  void Dispatch();
  void StartPlayback();
  penumbra::test::FakePlayer* Fake() { return port_.LatestPlayer(); }

  base::TestTaskEnvironment env_;
  std::shared_ptr<base::SequencedTaskRunner> sequence_runner_;
  std::shared_ptr<penumbra::ControlTaskRunner> control_;
  penumbra::test::FakePort port_;
  PlayerOptions options_;
  settings::Store settings_{settings::GetRegistry(),
                            settings::Platform::kLinux};
  std::unique_ptr<Player> player_;
  std::shared_ptr<pipeline::test::SourceControl> source_;
  std::vector<std::string> resolved_;
  std::vector<std::string> resolved_audio_languages_;
  bool resolved_prefer_descriptions_ = false;
  std::function<std::unique_ptr<pipeline::SampleSource>(std::string_view)>
      source_factory_;
  std::vector<PlayResult> results_;
  size_t notifications_ = 0;
  bool allow_playback_ = true;
};

}

#endif
