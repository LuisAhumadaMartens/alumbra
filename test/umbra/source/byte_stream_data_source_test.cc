#include "umbra/source/byte_stream_data_source.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fake_stream_provider.h"
#include "gtest/gtest.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_provider.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/status.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/base/url_redactor.h"
#include "umbra/source/data_source.h"
#include "umbra/source/read_request.h"

namespace umbra::source {
namespace {

using test::FakeStreamProvider;

std::vector<uint8_t> Pattern(size_t size) {
  std::vector<uint8_t> bytes(size);
  for (size_t i = 0; i < size; ++i) {
    bytes[i] = static_cast<uint8_t>((i * 13 + 5) & 0xFF);
  }
  return bytes;
}

std::vector<uint8_t> Slice(const std::vector<uint8_t>& bytes, size_t offset,
                           size_t size) {
  return {bytes.begin() + static_cast<std::ptrdiff_t>(offset),
          bytes.begin() + static_cast<std::ptrdiff_t>(offset + size)};
}

std::vector<uint8_t> ToVector(const penumbra::SharedBytes& bytes) {
  const std::span<const uint8_t> view = bytes.span();
  return {view.begin(), view.end()};
}

ByteStreamDataSourceOptions Threshold(size_t bytes) {
  ByteStreamDataSourceOptions options;
  options.pause_threshold_bytes = bytes;
  return options;
}

base::ErrorCode CodeOf(const absl::Status& status) {
  const auto detail = base::GetErrorDetail(status);
  EXPECT_TRUE(detail.ok() && detail->has_value());
  return detail->value().code;
}

class ByteStreamDataSourceTest : public ::testing::Test {
 protected:
  ByteStreamDataSourceTest()
      : runner_(environment_.CreateSequencedTaskRunner(
            base::TaskPriority::kControl)),
        redactor_(std::make_shared<base::UrlRedactor>()) {}

  void TearDown() override {
    if (source_ != nullptr) {
      Run([&] {
        source_->Close();
        source_.reset();
        open_result_.reset();
      });
    }
  }

  void Make(size_t size, bool seekable, bool size_known,
            ByteStreamDataSourceOptions options = {}) {
    content_ = Pattern(size);
    provider_ = std::make_shared<FakeStreamProvider>(content_, seekable,
                                                     size_known, 1000);
    Open(options);
  }

  void Open(ByteStreamDataSourceOptions options = {}) {
    open_result_.reset();
    Run([&] {
      penumbra::ByteStreamRequest request;
      request.url = "fake://media/movie.mp4";
      open_request_ = ByteStreamDataSource::Open(
          provider_, std::move(request), runner_, redactor_, options,
          [this](absl::StatusOr<std::shared_ptr<DataSource>> result) {
            open_result_ = std::move(result);
          });
    });
    Pump();
    if (open_result_.has_value() && open_result_->ok()) {
      source_ = std::static_pointer_cast<ByteStreamDataSource>(**open_result_);
    }
  }

  void Run(absl::AnyInvocable<void() &&> task) {
    ASSERT_TRUE(runner_->PostTask(std::move(task)));
    environment_.RunUntilIdle();
  }

  void Pump() {
    for (;;) {
      bool any = false;
      for (size_t i = 0; i < provider_->Streams().size(); ++i) {
        any = provider_->Step(i) || any;
      }
      environment_.RunUntilIdle();
      if (!any) {
        break;
      }
    }
  }

  std::shared_ptr<ReadRequest> StartRead(uint64_t offset, size_t size) {
    std::shared_ptr<ReadRequest> request;
    Run([&] {
      request = source_->Read(offset, size,
                              [this](absl::StatusOr<penumbra::SharedBytes> r) {
                                results_.push_back(std::move(r));
                              });
    });
    return request;
  }

  absl::StatusOr<penumbra::SharedBytes> ReadAndWait(uint64_t offset,
                                                    size_t size) {
    const size_t before = results_.size();
    StartRead(offset, size);
    Pump();
    EXPECT_EQ(results_.size(), before + 1);
    return results_.size() == before + 1 ? results_.back()
                                         : absl::InternalError("no result");
  }

  base::TestTaskEnvironment environment_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  std::shared_ptr<base::UrlRedactor> redactor_;
  std::vector<uint8_t> content_;
  std::shared_ptr<FakeStreamProvider> provider_;
  std::shared_ptr<OpenRequest> open_request_;
  std::optional<absl::StatusOr<std::shared_ptr<DataSource>>> open_result_;
  std::shared_ptr<ByteStreamDataSource> source_;
  std::vector<absl::StatusOr<penumbra::SharedBytes>> results_;
};

TEST_F(ByteStreamDataSourceTest, OpenReportsTheStreamInformation) {
  Make(5000, true, true);
  ASSERT_TRUE(open_result_.has_value());
  ASSERT_TRUE(open_result_->ok());
  const DataSourceInfo& info = source_->Info();
  EXPECT_TRUE(info.seekable);
  EXPECT_EQ(info.size, 5000U);
  EXPECT_EQ(info.content_type, "video/mp4");
  EXPECT_EQ(info.final_url, "fake://media/movie.mp4");
  ASSERT_EQ(provider_->Streams().size(), 1U);
  EXPECT_EQ(provider_->Streams()[0]->request.offset, 0U);
}

TEST_F(ByteStreamDataSourceTest, OpenFailureReachesTheCallbackOnce) {
  provider_ = std::make_shared<FakeStreamProvider>(Pattern(10), true, true, 10);
  provider_->FailOpenWith(absl::NotFoundError("no such file"));
  Open();
  ASSERT_TRUE(open_result_.has_value());
  ASSERT_FALSE(open_result_->ok());
  EXPECT_EQ(open_result_->status().code(), absl::StatusCode::kNotFound);
  EXPECT_EQ(CodeOf(open_result_->status()), base::ErrorCode::kSourceNotFound);
}

TEST_F(ByteStreamDataSourceTest, ProviderRejectionIsReportedAsAnError) {
  provider_ = std::make_shared<FakeStreamProvider>(Pattern(10), true, true, 10);
  provider_->RejectOpens(absl::PermissionDeniedError("denied"));
  Open();
  ASSERT_TRUE(open_result_.has_value());
  ASSERT_FALSE(open_result_->ok());
  EXPECT_EQ(CodeOf(open_result_->status()),
            base::ErrorCode::kSourceAccessDenied);
}

TEST_F(ByteStreamDataSourceTest, MapsOtherCanonicalCodes) {
  provider_ = std::make_shared<FakeStreamProvider>(Pattern(10), true, true, 10);
  provider_->FailOpenWith(absl::FailedPreconditionError("is a directory"));
  Open();
  ASSERT_FALSE(open_result_->ok());
  EXPECT_EQ(CodeOf(open_result_->status()),
            base::ErrorCode::kSourceNotReadable);

  provider_ = std::make_shared<FakeStreamProvider>(Pattern(10), true, true, 10);
  provider_->FailOpenWith(absl::DataLossError("disk"));
  Open();
  ASSERT_FALSE(open_result_->ok());
  EXPECT_EQ(CodeOf(open_result_->status()), base::ErrorCode::kSourceReadFailed);

  provider_ = std::make_shared<FakeStreamProvider>(Pattern(10), true, true, 10);
  provider_->FailOpenWith(absl::DeadlineExceededError("slow"));
  Open();
  ASSERT_FALSE(open_result_->ok());
  EXPECT_EQ(CodeOf(open_result_->status()), base::ErrorCode::kNetworkTimeout);
}

TEST_F(ByteStreamDataSourceTest, SequentialReadsReturnTheContent) {
  Make(5000, true, true);
  const auto first = ReadAndWait(0, 1500);
  ASSERT_TRUE(first.ok());
  EXPECT_EQ(ToVector(*first), Slice(content_, 0, 1500));
  const auto second = ReadAndWait(1500, 1500);
  ASSERT_TRUE(second.ok());
  EXPECT_EQ(ToVector(*second), Slice(content_, 1500, 1500));
  EXPECT_EQ(provider_->Streams().size(), 1U);
}

TEST_F(ByteStreamDataSourceTest, ReadAtTheEndIsShortAndThenEmpty) {
  Make(2500, true, false);
  const auto tail = ReadAndWait(2000, 1000);
  ASSERT_TRUE(tail.ok());
  EXPECT_EQ(ToVector(*tail), Slice(content_, 2000, 500));
  const auto past = ReadAndWait(2500, 10);
  ASSERT_TRUE(past.ok());
  EXPECT_TRUE(past->empty());
}

TEST_F(ByteStreamDataSourceTest, OffsetsPastAKnownSizeDeliverNothing) {
  Make(2500, true, true);
  const auto past = ReadAndWait(9000, 10);
  ASSERT_TRUE(past.ok());
  EXPECT_TRUE(past->empty());
  EXPECT_EQ(provider_->Streams().size(), 1U);
}

TEST_F(ByteStreamDataSourceTest, SeekingBackwardReopensAtTheOffset) {
  Make(8000, true, true);
  ASSERT_TRUE(ReadAndWait(0, 3000).ok());
  const auto back = ReadAndWait(500, 1000);
  ASSERT_TRUE(back.ok());
  EXPECT_EQ(ToVector(*back), Slice(content_, 500, 1000));
  ASSERT_EQ(provider_->Streams().size(), 2U);
  EXPECT_EQ(provider_->Streams()[1]->request.offset, 500U);
  EXPECT_TRUE(provider_->Streams()[0]->cancelled);
}

TEST_F(ByteStreamDataSourceTest, ASmallForwardGapIsSkippedWithoutReopening) {
  Make(20000, true, true);
  ASSERT_TRUE(ReadAndWait(0, 1000).ok());
  const auto ahead = ReadAndWait(5000, 1000);
  ASSERT_TRUE(ahead.ok());
  EXPECT_EQ(ToVector(*ahead), Slice(content_, 5000, 1000));
  EXPECT_EQ(provider_->Streams().size(), 1U);
}

TEST_F(ByteStreamDataSourceTest, ALargeForwardGapReopensAtTheOffset) {
  Make(400000, true, true);
  ASSERT_TRUE(ReadAndWait(0, 1000).ok());
  const auto far = ReadAndWait(390000, 1000);
  ASSERT_TRUE(far.ok());
  EXPECT_EQ(ToVector(*far), Slice(content_, 390000, 1000));
  ASSERT_EQ(provider_->Streams().size(), 2U);
  EXPECT_EQ(provider_->Streams()[1]->request.offset, 390000U);
}

TEST_F(ByteStreamDataSourceTest, ASeekInsideTheQueueIsServedFromIt) {
  Make(8000, true, true, Threshold(1000000));
  const auto inside = ReadAndWait(2500, 1000);
  ASSERT_TRUE(inside.ok());
  EXPECT_EQ(ToVector(*inside), Slice(content_, 2500, 1000));
  EXPECT_EQ(provider_->Streams().size(), 1U);
}

TEST_F(ByteStreamDataSourceTest, NonSeekableSourcesAcceptOnlyTheNextOffset) {
  Make(5000, false, false);
  const auto first = ReadAndWait(0, 1000);
  ASSERT_TRUE(first.ok());
  const auto next = ReadAndWait(1000, 1000);
  ASSERT_TRUE(next.ok());
  EXPECT_EQ(ToVector(*next), Slice(content_, 1000, 1000));
  const auto back = ReadAndWait(0, 10);
  ASSERT_FALSE(back.ok());
  EXPECT_EQ(back.status().code(), absl::StatusCode::kOutOfRange);
  const auto skip = ReadAndWait(4000, 10);
  ASSERT_FALSE(skip.ok());
  EXPECT_EQ(skip.status().code(), absl::StatusCode::kOutOfRange);
  EXPECT_EQ(provider_->Streams().size(), 1U);
}

TEST_F(ByteStreamDataSourceTest, PausesWhenTheQueueIsFullAndResumesAsItDrains) {
  Make(100000, true, true, Threshold(4000));
  Pump();
  const auto& stream = *provider_->Streams()[0];
  EXPECT_TRUE(stream.paused);
  EXPECT_EQ(stream.pauses, 1);
  EXPECT_TRUE(source_->Paused());
  EXPECT_GE(source_->QueuedBytes(), 4000U);
  EXPECT_LT(source_->QueuedBytes(), 6000U);

  StartRead(0, 3500);
  ASSERT_EQ(results_.size(), 1U);
  ASSERT_TRUE(results_[0].ok());
  EXPECT_LT(source_->QueuedBytes(), 2000U);
  EXPECT_EQ(stream.resumes, 1);
  EXPECT_FALSE(stream.paused);
  EXPECT_FALSE(source_->Paused());
  Pump();
  EXPECT_TRUE(stream.paused);
  EXPECT_EQ(stream.pauses, 2);
}

TEST_F(ByteStreamDataSourceTest, AReadLargerThanTheThresholdResumesTheStream) {
  Make(100000, true, true, Threshold(4000));
  Pump();
  ASSERT_TRUE(provider_->Streams()[0]->paused);
  const auto big = ReadAndWait(0, 20000);
  ASSERT_TRUE(big.ok());
  EXPECT_EQ(ToVector(*big), Slice(content_, 0, 20000));
}

TEST_F(ByteStreamDataSourceTest, ACancelledReadNeverCompletes) {
  Make(5000, true, true, Threshold(1000));
  const auto request = StartRead(0, 3000);
  Run([&] { request->Cancel(); });
  Pump();
  EXPECT_TRUE(results_.empty());
  const auto next = ReadAndWait(0, 1000);
  ASSERT_TRUE(next.ok());
  EXPECT_EQ(ToVector(*next), Slice(content_, 0, 1000));
}

TEST_F(ByteStreamDataSourceTest, ASecondConcurrentReadFails) {
  Make(5000, true, true, Threshold(1000));
  StartRead(0, 3000);
  StartRead(3000, 10);
  environment_.RunUntilIdle();
  ASSERT_EQ(results_.size(), 1U);
  EXPECT_EQ(results_[0].status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(ByteStreamDataSourceTest,
       AStreamErrorFailsTheReadAndTheNextReadRetries) {
  Make(5000, true, true, Threshold(1000));
  StartRead(0, 3000);
  provider_->FailStreamAfterOpen(0, absl::UnavailableError("disk went away"));
  environment_.RunUntilIdle();
  ASSERT_EQ(results_.size(), 1U);
  ASSERT_FALSE(results_[0].ok());
  EXPECT_EQ(CodeOf(results_[0].status()), base::ErrorCode::kSourceReadFailed);
  const auto retry = ReadAndWait(0, 2000);
  ASSERT_TRUE(retry.ok());
  EXPECT_EQ(ToVector(*retry), Slice(content_, 0, 2000));
  EXPECT_EQ(provider_->Streams().size(), 2U);
}

TEST_F(ByteStreamDataSourceTest, CloseCancelsTheStreamAndDropsLateCallbacks) {
  Make(5000, true, true, Threshold(1000));
  StartRead(0, 4000);
  provider_->Step(0);
  Run([&] { source_->Close(); });
  environment_.RunUntilIdle();
  EXPECT_TRUE(provider_->Streams()[0]->cancelled);
  EXPECT_TRUE(results_.empty());
  const auto after = ReadAndWait(0, 10);
  ASSERT_FALSE(after.ok());
  EXPECT_EQ(after.status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(ByteStreamDataSourceTest, CallbacksAfterDestructionAreDropped) {
  Make(5000, true, true);
  auto callbacks = provider_->Streams()[0]->callbacks;
  Run([&] {
    source_.reset();
    open_result_.reset();
  });
  penumbra::ByteStreamChunk chunk;
  chunk.data = penumbra::SharedBytes::Copy(std::vector<uint8_t>{1, 2, 3});
  EXPECT_TRUE(callbacks->OnData(std::move(chunk)).ok());
  environment_.RunUntilIdle();
  EXPECT_TRUE(provider_->Streams()[0]->cancelled);
}

TEST_F(ByteStreamDataSourceTest, CancellingTheOpenRequestClosesTheSource) {
  content_ = Pattern(100);
  provider_ = std::make_shared<FakeStreamProvider>(content_, true, true, 10);
  Run([&] {
    penumbra::ByteStreamRequest request;
    request.url = "fake://media/movie.mp4";
    open_request_ = ByteStreamDataSource::Open(
        provider_, std::move(request), runner_, redactor_, {},
        [this](absl::StatusOr<std::shared_ptr<DataSource>> result) {
          open_result_ = std::move(result);
        });
    open_request_->Cancel();
  });
  Pump();
  EXPECT_FALSE(open_result_.has_value());
  EXPECT_TRUE(provider_->Streams()[0]->cancelled);
}

TEST_F(ByteStreamDataSourceTest, OpenRejectsMissingArguments) {
  Run([&] {
    open_request_ = ByteStreamDataSource::Open(
        nullptr, {}, runner_, redactor_, {},
        [this](absl::StatusOr<std::shared_ptr<DataSource>> result) {
          open_result_ = std::move(result);
        });
  });
  ASSERT_TRUE(open_result_.has_value());
  EXPECT_EQ(open_result_->status().code(), absl::StatusCode::kInvalidArgument);
}

}
}
