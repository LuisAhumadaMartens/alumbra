#include "umbra/observability/port_sink.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "penumbra/interface/log_sink.h"
#include "penumbra/interface/port_types.h"
#include "umbra/observability/category.h"
#include "umbra/observability/log.h"

namespace umbra::observability {
namespace {

class RecordingPort final : public penumbra::LogSink {
 public:
  absl::Status Log(penumbra::LogLevel level,
                   std::string_view redacted_message) override {
    levels.push_back(level);
    lines.emplace_back(redacted_message);
    return reject ? absl::UnavailableError("rejected") : absl::OkStatus();
  }

  std::vector<penumbra::LogLevel> levels;
  std::vector<std::string> lines;
  bool reject = false;
};

class PortSinkTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SetSink(std::make_shared<PortSink>(port_));
    SetLogLevel(Level::kVerbose);
    SetLogCategoryMask(kAllCategories);
  }

  void TearDown() override {
    SetSink(nullptr);
    SetLogLevel(Level::kInfo);
  }

  std::shared_ptr<RecordingPort> port_ = std::make_shared<RecordingPort>();
};

TEST_F(PortSinkTest, FormatsOneRedactedLineAndMapsTheSeverity) {
  ALUMBRA_LOG(kNet, kError)
          .Field("throughput_bps", 2'500'000)
          .Field("note", "two words")
          .Field("ok", false)
      << "request failed";
  ASSERT_EQ(port_->lines.size(), 1U);
  EXPECT_EQ(port_->lines[0],
            "[net] request failed throughput_bps=2500000 note=\"two words\" "
            "ok=false");
  EXPECT_EQ(port_->levels[0], penumbra::LogLevel::kError);
}

TEST_F(PortSinkTest, EverySeverityMapsToThePortLevel) {
  ALUMBRA_LOG(kPort, kError) << "e";
  ALUMBRA_LOG(kPort, kWarning) << "w";
  ALUMBRA_LOG(kPort, kInfo) << "i";
  EXPECT_EQ(port_->levels,
            (std::vector<penumbra::LogLevel>{penumbra::LogLevel::kError,
                                             penumbra::LogLevel::kWarning,
                                             penumbra::LogLevel::kInfo}));
}

TEST_F(PortSinkTest, CountsMessagesTheSinkRejects) {
  auto sink = std::make_shared<PortSink>(port_);
  SetSink(sink);
  port_->reject = true;
  ALUMBRA_LOG(kPort, kError) << "dropped";
  EXPECT_EQ(sink->Rejected(), 1U);
}

}
}
