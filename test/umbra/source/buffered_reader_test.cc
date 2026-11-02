#include "umbra/source/buffered_reader.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fake_data_source.h"
#include "gtest/gtest.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/test_support/test_task_environment.h"
#include "umbra/source/data_source.h"
#include "umbra/source/read_request.h"

namespace umbra::source {
namespace {

using test::FakeDataSource;

std::vector<uint8_t> Pattern(size_t size) {
  std::vector<uint8_t> bytes(size);
  for (size_t i = 0; i < size; ++i) {
    bytes[i] = static_cast<uint8_t>((i * 7 + 3) & 0xFF);
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

class BufferedReaderTest : public ::testing::Test {
 protected:
  BufferedReaderTest()
      : runner_(environment_.CreateSequencedTaskRunner(
            base::TaskPriority::kControl)) {}

  void Make(size_t content_size, bool seekable, bool size_known,
            BufferedReaderOptions options = {
                .read_ahead_bytes = kMinReadAheadBytes, .chunk_bytes = 16384}) {
    content_ = Pattern(content_size);
    source_ = std::make_shared<FakeDataSource>(content_, seekable, size_known,
                                               runner_);
    auto reader = BufferedReader::Create(source_, runner_, options);
    ASSERT_TRUE(reader.ok());
    reader_ = *std::move(reader);
  }

  void Run(absl::AnyInvocable<void() &&> task) {
    ASSERT_TRUE(runner_->PostTask(std::move(task)));
    environment_.RunUntilIdle();
  }

  std::shared_ptr<ReadRequest> StartRead(uint64_t offset, size_t size) {
    std::shared_ptr<ReadRequest> request;
    Run([&] {
      request = reader_->Read(offset, size,
                              [this](absl::StatusOr<penumbra::SharedBytes> r) {
                                results_.push_back(std::move(r));
                              });
    });
    return request;
  }

  void Pump() {
    for (int i = 0; i < 1000 && source_->InflightCount() > 0; ++i) {
      source_->DeliverAll();
      environment_.RunUntilIdle();
    }
  }

  absl::StatusOr<penumbra::SharedBytes> ReadAndWait(uint64_t offset,
                                                    size_t size) {
    const size_t before = results_.size();
    StartRead(offset, size);
    Pump();
    environment_.RunUntilIdle();
    EXPECT_EQ(results_.size(), before + 1);
    return results_.size() == before + 1 ? results_.back()
                                         : absl::InternalError("no result");
  }

  base::TestTaskEnvironment environment_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  std::vector<uint8_t> content_;
  std::shared_ptr<FakeDataSource> source_;
  std::unique_ptr<BufferedReader> reader_;
  std::vector<absl::StatusOr<penumbra::SharedBytes>> results_;
};

TEST_F(BufferedReaderTest, CreateValidatesItsArguments) {
  Make(1000, true, true);
  EXPECT_FALSE(BufferedReader::Create(nullptr, runner_).ok());
  EXPECT_FALSE(BufferedReader::Create(source_, nullptr).ok());
  EXPECT_FALSE(
      BufferedReader::Create(
          source_, runner_,
          {.read_ahead_bytes = kMinReadAheadBytes - 1, .chunk_bytes = 1})
          .ok());
  EXPECT_FALSE(
      BufferedReader::Create(
          source_, runner_,
          {.read_ahead_bytes = kMaxReadAheadBytes + 1, .chunk_bytes = 1})
          .ok());
  EXPECT_FALSE(BufferedReader::Create(
                   source_, runner_,
                   {.read_ahead_bytes = kMinReadAheadBytes, .chunk_bytes = 0})
                   .ok());
  EXPECT_TRUE(BufferedReader::Create(
                  source_, runner_,
                  {.read_ahead_bytes = kMaxReadAheadBytes, .chunk_bytes = 1})
                  .ok());
  EXPECT_EQ(kDefaultReadAheadBytes, 2097152U);
}

TEST_F(BufferedReaderTest, ReadsAheadWithinTheConfiguredWindow) {
  Make(300000, true, true);
  const auto first = ReadAndWait(0, 1000);
  ASSERT_TRUE(first.ok());
  EXPECT_EQ(ToVector(*first), Slice(content_, 0, 1000));
  EXPECT_EQ(source_->Log().size(), 5U);
  const auto& last = source_->Log().back();
  EXPECT_EQ(last.offset + last.size, 1000U + kMinReadAheadBytes);
  EXPECT_EQ(source_->InflightCount(), 0U);
}

TEST_F(BufferedReaderTest, ServesBufferedRangesWithoutAnotherFetch) {
  Make(300000, true, true);
  ASSERT_TRUE(ReadAndWait(0, 1000).ok());
  const size_t fetches = source_->Log().size();
  const auto second = ReadAndWait(1000, 5000);
  ASSERT_TRUE(second.ok());
  EXPECT_EQ(ToVector(*second), Slice(content_, 1000, 5000));
  EXPECT_EQ(source_->Log().size(), fetches + 1);
  EXPECT_EQ(source_->Log().back().offset, 1000U + kMinReadAheadBytes);
  EXPECT_EQ(source_->Log().back().size, 5000U);
}

TEST_F(BufferedReaderTest, NeverFetchesTheSameBytesTwiceWhileReadingForward) {
  Make(300000, true, true);
  ASSERT_TRUE(ReadAndWait(0, 4096).ok());
  ASSERT_TRUE(ReadAndWait(0, 65536).ok());
  ASSERT_TRUE(ReadAndWait(0, 30000).ok());
  uint64_t expected = 0;
  for (const auto& entry : source_->Log()) {
    EXPECT_EQ(entry.offset, expected);
    expected += entry.size;
  }
}

TEST_F(BufferedReaderTest, ShortReadsMarkTheEndOfAnUnknownSizeSource) {
  Make(20000, true, false);
  const auto all = ReadAndWait(0, 30000);
  ASSERT_TRUE(all.ok());
  EXPECT_EQ(ToVector(*all), content_);
  const size_t fetches = source_->Log().size();
  const auto end = ReadAndWait(20000, 10);
  ASSERT_TRUE(end.ok());
  EXPECT_TRUE(end->empty());
  EXPECT_EQ(source_->Log().size(), fetches);
  EXPECT_EQ(reader_->Info().size, std::nullopt);
}

TEST_F(BufferedReaderTest, KnownSizeClampsFetchesAndEndsEmpty) {
  Make(20000, true, true);
  const auto tail = ReadAndWait(19990, 100);
  ASSERT_TRUE(tail.ok());
  EXPECT_EQ(ToVector(*tail), Slice(content_, 19990, 10));
  for (const auto& entry : source_->Log()) {
    EXPECT_LE(entry.offset + entry.size, 20000U);
  }
  const auto beyond = ReadAndWait(25000, 10);
  ASSERT_TRUE(beyond.ok());
  EXPECT_TRUE(beyond->empty());
}

TEST_F(BufferedReaderTest, SeekableSourceRestartsOnABackwardOrDistantRead) {
  Make(900000, true, true);
  ASSERT_TRUE(ReadAndWait(0, 1000).ok());
  const auto far = ReadAndWait(500000, 100);
  ASSERT_TRUE(far.ok());
  EXPECT_EQ(ToVector(*far), Slice(content_, 500000, 100));
  const auto back = ReadAndWait(10, 100);
  ASSERT_TRUE(back.ok());
  EXPECT_EQ(ToVector(*back), Slice(content_, 10, 100));
  EXPECT_LT(reader_->BufferedBytes(), 400000U);
}

TEST_F(BufferedReaderTest, NonSeekableSourceReadsForwardAndRejectsRewinds) {
  Make(300000, false, false);
  ASSERT_TRUE(ReadAndWait(0, 100).ok());
  const auto ahead = ReadAndWait(100000, 100);
  ASSERT_TRUE(ahead.ok());
  EXPECT_EQ(ToVector(*ahead), Slice(content_, 100000, 100));
  const auto back = ReadAndWait(0, 100);
  EXPECT_EQ(back.status().code(), absl::StatusCode::kOutOfRange);
}

TEST_F(BufferedReaderTest, PropagatesAFailureOnce) {
  Make(100000, true, true);
  source_->FailNext(absl::UnavailableError("offline"));
  const auto failed = ReadAndWait(0, 100);
  EXPECT_EQ(failed.status().code(), absl::StatusCode::kUnavailable);
  const auto retried = ReadAndWait(0, 100);
  ASSERT_TRUE(retried.ok());
  EXPECT_EQ(ToVector(*retried), Slice(content_, 0, 100));
}

TEST_F(BufferedReaderTest, ReadAheadFailureWaitsForTheNextReadThatNeedsData) {
  Make(300000, true, true);
  ASSERT_TRUE(ReadAndWait(0, 100).ok());
  source_->FailNext(absl::DataLossError("late"));
  ASSERT_TRUE(ReadAndWait(60000, 100).ok());
  const auto covered = ReadAndWait(62000, 100);
  ASSERT_TRUE(covered.ok());
  EXPECT_EQ(ToVector(*covered), Slice(content_, 62000, 100));
  const auto missing = ReadAndWait(100000, 100);
  EXPECT_EQ(missing.status().code(), absl::StatusCode::kDataLoss);
  const auto retried = ReadAndWait(100000, 100);
  ASSERT_TRUE(retried.ok());
  EXPECT_EQ(ToVector(*retried), Slice(content_, 100000, 100));
}

TEST_F(BufferedReaderTest, CancellingBeforeCompletionDeliversNothing) {
  Make(100000, true, true);
  auto request = StartRead(0, 100);
  ASSERT_EQ(source_->InflightCount(), 1U);
  ASSERT_TRUE(runner_->PostTask([request] { request->Cancel(); }));
  source_->DeliverAll();
  environment_.RunUntilIdle();
  EXPECT_TRUE(results_.empty());
  Run([request] { EXPECT_TRUE(request->Cancelled()); });
}

TEST_F(BufferedReaderTest, CancellingAfterTheResultIsQueuedDeliversNothing) {
  Make(100000, true, true);
  auto request = StartRead(0, 100);
  source_->DeliverNext();
  ASSERT_TRUE(runner_->PostTask([request] { request->Cancel(); }));
  environment_.RunUntilIdle();
  EXPECT_TRUE(results_.empty());
  Pump();
  EXPECT_TRUE(results_.empty());
}

TEST_F(BufferedReaderTest, ANewReadReplacesACancelledOne) {
  Make(100000, true, true);
  auto cancelled = StartRead(0, 100);
  Run([cancelled] { cancelled->Cancel(); });
  const auto next = ReadAndWait(50, 100);
  ASSERT_TRUE(next.ok());
  EXPECT_EQ(ToVector(*next), Slice(content_, 50, 100));
  EXPECT_EQ(results_.size(), 1U);
}

TEST_F(BufferedReaderTest, RejectsOverlappingZeroSizedAndOverflowingReads) {
  Make(100000, true, true);
  StartRead(0, 100);
  StartRead(0, 100);
  environment_.RunUntilIdle();
  ASSERT_EQ(results_.size(), 1U);
  EXPECT_EQ(results_[0].status().code(), absl::StatusCode::kFailedPrecondition);
  Pump();
  ASSERT_EQ(results_.size(), 2U);
  EXPECT_TRUE(results_[1].ok());
  const auto empty = ReadAndWait(5, 0);
  ASSERT_TRUE(empty.ok());
  EXPECT_TRUE(empty->empty());
  const auto overflow = ReadAndWait(UINT64_MAX - 1, 10);
  EXPECT_EQ(overflow.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(BufferedReaderTest, CloseCancelsWorkAndRejectsLaterReads) {
  Make(100000, true, true);
  auto request = StartRead(0, 100);
  Run([this] { reader_->Close(); });
  EXPECT_TRUE(source_->Closed());
  source_->DeliverAll();
  environment_.RunUntilIdle();
  EXPECT_TRUE(results_.empty());
  Run([request] { EXPECT_TRUE(request->Cancelled()); });
  const auto late = ReadAndWait(0, 10);
  EXPECT_EQ(late.status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(BufferedReaderTest, DestroyingTheReaderLeavesNoCallbacks) {
  Make(100000, true, true);
  StartRead(0, 100);
  Run([this] { reader_.reset(); });
  source_->DeliverAll();
  environment_.RunUntilIdle();
  EXPECT_TRUE(results_.empty());
}

TEST(ReadRequestTest, CompletesOnceAndCancelReleasesTheCallback) {
  base::TestTaskEnvironment environment;
  auto runner =
      environment.CreateSequencedTaskRunner(base::TaskPriority::kControl);
  int calls = 0;
  auto token = std::make_shared<int>(0);
  runner->PostTask([&] {
    auto first = ReadRequest::Create(
        [&calls](const absl::StatusOr<penumbra::SharedBytes>&) { ++calls; });
    EXPECT_TRUE(first->Complete(penumbra::SharedBytes()));
    EXPECT_FALSE(first->Complete(penumbra::SharedBytes()));
    EXPECT_TRUE(first->Completed());
    first->Cancel();
    EXPECT_FALSE(first->Cancelled());

    const std::weak_ptr<int> weak = token;
    auto second = ReadRequest::Create(
        [&calls, held = std::move(token)](
            const absl::StatusOr<penumbra::SharedBytes>&) { ++calls; });
    second->Cancel();
    second->Cancel();
    EXPECT_TRUE(weak.expired());
    EXPECT_FALSE(second->Complete(penumbra::SharedBytes()));
    EXPECT_TRUE(second->Cancelled());
  });
  environment.RunUntilIdle();
  EXPECT_EQ(calls, 1);
}

TEST(ReadRequestTest, CancelRacingWithCompletionNeverCallsBackAfterCancel) {
  base::TestTaskEnvironment environment;
  auto runner =
      environment.CreateSequencedTaskRunner(base::TaskPriority::kControl);
  constexpr int kRounds = 200;
  std::vector<std::shared_ptr<ReadRequest>> requests(kRounds);
  std::vector<bool> cancel_ran(kRounds, false);
  int callbacks_after_cancel = 0;
  int callbacks = 0;
  runner->PostTask([&] {
    for (int i = 0; i < kRounds; ++i) {
      requests[i] = ReadRequest::Create(
          [&, i](const absl::StatusOr<penumbra::SharedBytes>&) {
            ++callbacks;
            if (cancel_ran[i]) {
              ++callbacks_after_cancel;
            }
          });
    }
  });
  environment.RunUntilIdle();
  std::thread network([&] {
    for (int i = 0; i < kRounds; ++i) {
      runner->PostTask(
          [&, i] { requests[i]->Complete(penumbra::SharedBytes()); });
    }
  });
  for (int i = 0; i < kRounds; ++i) {
    runner->PostTask([&, i] {
      cancel_ran[i] = true;
      requests[i]->Cancel();
    });
  }
  network.join();
  environment.RunUntilIdle();
  EXPECT_EQ(callbacks_after_cancel, 0);
  EXPECT_LE(callbacks, kRounds);
  runner->PostTask([&] {
    for (int i = 0; i < kRounds; ++i) {
      EXPECT_TRUE(requests[i]->Cancelled() || requests[i]->Completed());
    }
  });
  environment.RunUntilIdle();
}

}
}
