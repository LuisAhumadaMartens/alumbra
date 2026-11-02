#include "umbra/source/source_type_detector.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fake_data_source.h"
#include "gtest/gtest.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/status.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/base/url_redactor.h"
#include "umbra/source/buffered_reader.h"
#include "umbra/source/source_type.h"

namespace umbra::source {
namespace {

SniffHints Hints(std::string extension = {}, std::string content_type = {}) {
  return {.extension = std::move(extension),
          .content_type = std::move(content_type)};
}

std::vector<uint8_t> Bytes(std::string_view text) {
  return {text.begin(), text.end()};
}

class DetectorTest : public ::testing::Test {
 protected:
  DetectorTest()
      : runner_(environment_.CreateSequencedTaskRunner(
            base::TaskPriority::kControl)),
        redactor_(std::make_shared<const base::UrlRedactor>(
            std::array<std::uint64_t, 2>{1, 2})) {}

  void Open(std::vector<uint8_t> content) {
    source_ = std::make_shared<test::FakeDataSource>(std::move(content), true,
                                                     false, runner_);
    auto reader = BufferedReader::Create(
        source_, runner_,
        {.read_ahead_bytes = kMinReadAheadBytes, .chunk_bytes = kSniffBytes});
    ASSERT_TRUE(reader.ok());
    reader_ = *std::move(reader);
    detector_ = std::make_unique<SourceTypeDetector>(reader_.get(), redactor_);
  }

  void Run(absl::AnyInvocable<void() &&> task) {
    ASSERT_TRUE(runner_->PostTask(std::move(task)));
    environment_.RunUntilIdle();
  }

  absl::StatusOr<DetectedType> Detect(SniffHints hints = {}) {
    std::optional<absl::StatusOr<DetectedType>> result;
    Run([&] {
      detector_->Start(std::move(hints),
                       [&result](absl::StatusOr<DetectedType> detected) {
                         result = std::move(detected);
                       });
    });
    for (int i = 0; i < 100 && !result.has_value(); ++i) {
      source_->DeliverAll();
      environment_.RunUntilIdle();
    }
    EXPECT_TRUE(result.has_value());
    return result.value_or(absl::InternalError("no result"));
  }

  base::TestTaskEnvironment environment_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  std::shared_ptr<const base::UrlRedactor> redactor_;
  std::shared_ptr<test::FakeDataSource> source_;
  std::unique_ptr<BufferedReader> reader_;
  std::unique_ptr<SourceTypeDetector> detector_;
};

TEST_F(DetectorTest, SniffsTheFirstBytesOfTheResponse) {
  Open(Bytes("#EXTM3U\n#EXT-X-VERSION:7\n"));
  const auto detected =
      Detect({.extension = "mp4", .content_type = "video/mp4"});
  ASSERT_TRUE(detected.ok());
  EXPECT_EQ(detected->type, SourceType::kHls);
  EXPECT_EQ(detected->signal, TypeSignal::kSignature);
}

TEST_F(DetectorTest, BreaksTiesWithTheExtensionOrContentType) {
  Open(Bytes("opaque tokenised playlist\n"));
  const auto detected = Detect(Hints({}, "application/x-mpegURL"));
  ASSERT_TRUE(detected.ok());
  EXPECT_EQ(detected->type, SourceType::kHls);
  EXPECT_EQ(detected->signal, TypeSignal::kContentType);
}

TEST_F(DetectorTest, GrowsToSixtyFourKibibytesWhileAPrologueHidesTheRoot) {
  std::string text = "<?xml version=\"1.0\"?>\n";
  while (text.size() < 20000) {
    text += "<!-- padding padding padding padding -->\n";
  }
  text += "<MPD type=\"static\"/>";
  Open(Bytes(text));
  const auto detected = Detect();
  ASSERT_TRUE(detected.ok());
  EXPECT_EQ(detected->type, SourceType::kDash);
  EXPECT_LE(source_->Log().back().offset + source_->Log().back().size,
            kMaxSniffBytes + kSniffBytes);
}

TEST_F(DetectorTest, ParserReadsAfterSniffingReuseTheBufferedBytes) {
  Open(Bytes("#EXTM3U\n#EXT-X-VERSION:7\n"));
  ASSERT_TRUE(Detect().ok());
  const size_t fetches = source_->Log().size();
  std::optional<absl::StatusOr<penumbra::SharedBytes>> head;
  Run([&] {
    reader_->Read(0, 7, [&head](absl::StatusOr<penumbra::SharedBytes> bytes) {
      head = std::move(bytes);
    });
  });
  ASSERT_TRUE(head.has_value());
  ASSERT_TRUE(head->ok());
  const std::span<const uint8_t> view = (*head)->span();
  EXPECT_EQ(std::string(view.begin(), view.end()), "#EXTM3U");
  EXPECT_EQ(source_->Log().size(), fetches);
}

TEST_F(DetectorTest, UndecidedBytesFailWithTheSourceTypeError) {
  Open(Bytes("completely unknown content"));
  const auto detected = Detect();
  ASSERT_FALSE(detected.ok());
  EXPECT_EQ(detected.status().code(), absl::StatusCode::kUnimplemented);
  const auto detail = base::GetErrorDetail(detected.status());
  ASSERT_TRUE(detail.ok());
  ASSERT_TRUE(detail->has_value());
  EXPECT_EQ((*detail)->code, base::ErrorCode::kSourceTypeUnsupported);
  EXPECT_EQ((*detail)->context.at("bytes_inspected"), "26");
}

TEST_F(DetectorTest, ReadFailuresPropagate) {
  Open(Bytes("#EXTM3U\n"));
  source_->FailNext(absl::UnavailableError("offline"));
  const auto detected = Detect();
  EXPECT_EQ(detected.status().code(), absl::StatusCode::kUnavailable);
}

TEST_F(DetectorTest, CancellingSuppressesTheCallback) {
  Open(Bytes("#EXTM3U\n"));
  bool called = false;
  Run([&] {
    detector_->Start(
        {}, [&called](const absl::StatusOr<DetectedType>&) { called = true; });
  });
  Run([this] { detector_->Cancel(); });
  for (int i = 0; i < 10; ++i) {
    source_->DeliverAll();
    environment_.RunUntilIdle();
  }
  EXPECT_FALSE(called);
}

}
}
