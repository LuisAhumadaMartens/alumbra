#ifndef TEST_PENUMBRA_DESKTOP_GPU_TEST_ENVIRONMENT_H_
#define TEST_PENUMBRA_DESKTOP_GPU_TEST_ENVIRONMENT_H_

#include <SDL3/SDL_stdinc.h>
#include <gtest/gtest.h>

#include <memory>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/desktop/video/gpu_presenter.h"
#include "penumbra/desktop/video/sdl_session.h"

namespace penumbra::desktop::testing {

inline bool GpuRequired() {
  const char* value = SDL_getenv("ALUMBRA_REQUIRE_GPU");
  return value != nullptr && std::string_view(value) == "1";
}

class GpuTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::StatusOr<std::unique_ptr<SdlVideoSession>> session =
        SdlVideoSession::Create(SessionMode::kHeadless);
    if (!session.ok()) {
      Unavailable(session.status());
      return;
    }
    session_ = *std::move(session);
    available_ = true;
  }

  static void Unavailable(const absl::Status& status) {
    if (GpuRequired()) {
      FAIL() << "A GPU device is required: " << status;
    }
    GTEST_SKIP() << "No GPU device: " << status;
  }

  static std::unique_ptr<GpuPresenter> CreatePresenter(
      const GpuPresenterOptions& options = {}) {
    absl::StatusOr<std::unique_ptr<GpuPresenter>> presenter =
        GpuPresenter::Create(options);
    if (!presenter.ok()) {
      Unavailable(presenter.status());
      return nullptr;
    }
    return *std::move(presenter);
  }

  std::unique_ptr<SdlVideoSession> session_;
  bool available_ = false;
};

}

#endif
