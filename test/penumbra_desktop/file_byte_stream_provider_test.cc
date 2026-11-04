#include "penumbra/desktop/file_byte_stream_provider.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <ios>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_provider.h"
#include "umbra/base/error_catalogue.h"
#include "umbra/base/single_thread_task_executor.h"
#include "umbra/base/status.h"
#include "umbra/base/task_runner.h"
#include "umbra/base/tick_clock.h"
#include "umbra/base/url_redactor.h"
#include "umbra/source/byte_stream_data_source.h"
#include "umbra/source/data_source.h"
#include "umbra/source/file_url.h"
#include "umbra/source/read_request.h"
#include "umbra/source/scheme_registry.h"
#include "umbra/source/source_resolver.h"

namespace penumbra::desktop {
namespace {

namespace fs = std::filesystem;

constexpr uint64_t kFourGiB = 4294967296ULL;

class Collector final : public ByteStreamCallbacks {
 public:
  absl::Status OnOpen(ByteStreamInfo info) override {
    const std::scoped_lock lock(mutex_);
    info_ = std::move(info);
    wake_.notify_all();
    return absl::OkStatus();
  }
  absl::Status OnData(ByteStreamChunk chunk) override {
    const std::scoped_lock lock(mutex_);
    const auto view = chunk.data.span();
    data_.insert(data_.end(), view.begin(), view.end());
    ++chunks_;
    wake_.notify_all();
    return absl::OkStatus();
  }
  absl::Status OnEnd() override {
    const std::scoped_lock lock(mutex_);
    ended_ = true;
    wake_.notify_all();
    return absl::OkStatus();
  }
  absl::Status OnError(absl::Status error) override {
    const std::scoped_lock lock(mutex_);
    error_ = std::move(error);
    wake_.notify_all();
    return absl::OkStatus();
  }

  bool WaitFor(const std::function<bool()>& done) {
    std::unique_lock lock(mutex_);
    return wake_.wait_for(lock, std::chrono::seconds(20), done);
  }

  bool WaitTerminal() {
    return WaitFor([this] { return ended_ || error_.has_value(); });
  }

  std::mutex mutex_;
  std::condition_variable wake_;
  std::optional<ByteStreamInfo> info_;
  std::vector<uint8_t> data_;
  size_t chunks_ = 0;
  bool ended_ = false;
  std::optional<absl::Status> error_;
};

class FileProviderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = fs::path(ALUMBRA_FILE_TEST_DIRECTORY) /
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
    fs::remove_all(root_);
    fs::create_directories(root_);
  }

  void TearDown() override {
    std::error_code ec;
    fs::permissions(root_ / "locked.bin", fs::perms::owner_all,
                    fs::perm_options::replace, ec);
    fs::remove_all(root_, ec);
  }

  static std::vector<uint8_t> Pattern(size_t size, size_t seed) {
    std::vector<uint8_t> bytes(size);
    for (size_t i = 0; i < size; ++i) {
      bytes[i] = static_cast<uint8_t>((i * 31 + seed) & 0xFF);
    }
    return bytes;
  }

  fs::path WriteFile(const std::string& name,
                     const std::vector<uint8_t>& bytes) {
    const fs::path path =
        root_ / fs::path(std::u8string(name.begin(), name.end()));
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return path;
  }

  static std::string UrlFor(const fs::path& path) {
    const auto url = umbra::source::FileUrlFromPath(path.string());
    EXPECT_TRUE(url.ok());
    return url.ok() ? *url : std::string();
  }

  std::shared_ptr<Collector> OpenAndCollect(
      const std::string& url, uint64_t offset = 0,
      std::optional<uint64_t> length = std::nullopt) {
    auto collector = std::make_shared<Collector>();
    ByteStreamRequest request;
    request.url = url;
    request.offset = offset;
    request.length = length;
    auto stream = provider_.Open(std::move(request), collector);
    EXPECT_TRUE(stream.ok());
    if (stream.ok()) {
      streams_.push_back(*std::move(stream));
      collector->WaitTerminal();
    }
    return collector;
  }

  fs::path root_;
  FileByteStreamProvider provider_;
  std::vector<std::unique_ptr<ByteStream>> streams_;
};

TEST_F(FileProviderTest, ServesTheSchemeFile) {
  EXPECT_EQ(provider_.Schemes(), std::vector<std::string>{"file"});
}

TEST_F(FileProviderTest, EnumeratesSiblingFileUrlsAsynchronously) {
  const std::string media_url = UrlFor(WriteFile("movie.mkv", Pattern(1, 1)));
  const std::string plain_url = UrlFor(WriteFile("movie.srt", Pattern(1, 2)));
  const std::string language_url =
      UrlFor(WriteFile("movie.en.vtt", Pattern(1, 3)));
  const std::string unrelated_url =
      UrlFor(WriteFile("poster.png", Pattern(1, 4)));
  std::promise<absl::StatusOr<std::vector<std::string>>> promise;
  std::future<absl::StatusOr<std::vector<std::string>>> result =
      promise.get_future();
  provider_.ListSiblingFileUrls(
      media_url, [&promise](absl::StatusOr<std::vector<std::string>> urls) {
        promise.set_value(std::move(urls));
      });
  ASSERT_EQ(result.wait_for(std::chrono::seconds(20)),
            std::future_status::ready);
  auto urls = result.get();
  ASSERT_TRUE(urls.ok());
  EXPECT_EQ(*urls, (std::vector<std::string>{language_url, media_url, plain_url,
                                             unrelated_url}));
}

TEST_F(FileProviderTest, ReportsSizeAndSeekabilityAndNoContentType) {
  const auto bytes = Pattern(200000, 1);
  const std::string url = UrlFor(WriteFile("a.bin", bytes));
  const auto collector = OpenAndCollect(url);
  ASSERT_TRUE(collector->ended_);
  ASSERT_TRUE(collector->info_.has_value());
  EXPECT_TRUE(collector->info_->seekable);
  EXPECT_EQ(collector->info_->size, bytes.size());
  EXPECT_TRUE(collector->info_->content_type.empty());
  EXPECT_EQ(collector->info_->final_url, url);
  EXPECT_EQ(collector->data_, bytes);
  EXPECT_GT(collector->chunks_, 1U);
}

TEST_F(FileProviderTest, HonoursOffsetAndLength) {
  const auto bytes = Pattern(100000, 2);
  const std::string url = UrlFor(WriteFile("a.bin", bytes));
  const auto collector = OpenAndCollect(url, 70000, 1234);
  ASSERT_TRUE(collector->ended_);
  EXPECT_EQ(collector->data_,
            std::vector<uint8_t>(bytes.begin() + 70000, bytes.begin() + 71234));
  const auto past = OpenAndCollect(url, 100000);
  ASSERT_TRUE(past->ended_);
  EXPECT_TRUE(past->data_.empty());
}

TEST_F(FileProviderTest, ReadsAcrossAFourGibOffsetInASparseFile) {
  const fs::path path = root_ / "sparse.bin";
  {
    const std::ofstream out(path, std::ios::binary);
  }
  fs::resize_file(path, kFourGiB + 4096);
  const std::vector<uint8_t> tail = Pattern(300, 9);
  {
    std::fstream out(path, std::ios::binary | std::ios::in | std::ios::out);
    out.seekp(static_cast<std::streamoff>(kFourGiB - 100));
    out.write(reinterpret_cast<const char*>(tail.data()),
              static_cast<std::streamsize>(tail.size()));
  }
  const std::string url = UrlFor(path);
  const auto collector = OpenAndCollect(url, kFourGiB - 100, 300);
  ASSERT_TRUE(collector->ended_);
  ASSERT_TRUE(collector->info_.has_value());
  EXPECT_EQ(collector->info_->size, kFourGiB + 4096);
  EXPECT_EQ(collector->data_, tail);
}

TEST_F(FileProviderTest, OpensPathsWithSpacesAndNonAsciiCharacters) {
  const auto bytes = Pattern(5000, 3);
  const fs::path path = WriteFile(
      "pel\xC3\xAD"
      "cula \xE6\x98\xA0\xE7\x94\xBB #1.mkv",
      bytes);
  const auto from_url = OpenAndCollect(UrlFor(path));
  ASSERT_TRUE(from_url->ended_);
  EXPECT_EQ(from_url->data_, bytes);
  const auto localhost =
      OpenAndCollect("file://localhost" + UrlFor(path).substr(7));
  ASSERT_TRUE(localhost->ended_);
  EXPECT_EQ(localhost->data_, bytes);
}

TEST_F(FileProviderTest, AMissingFileIsNotFound) {
  const auto collector = OpenAndCollect(UrlFor(root_ / "missing.mkv"));
  ASSERT_TRUE(collector->error_.has_value());
  EXPECT_EQ(collector->error_->code(), absl::StatusCode::kNotFound);
  EXPECT_FALSE(collector->info_.has_value());
}

TEST_F(FileProviderTest, ADirectoryIsAFailedPrecondition) {
  const auto collector = OpenAndCollect(UrlFor(root_));
  ASSERT_TRUE(collector->error_.has_value());
  EXPECT_EQ(collector->error_->code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(FileProviderTest, AnUnreadableFileIsPermissionDenied) {
  const fs::path path = WriteFile("locked.bin", Pattern(100, 4));
  fs::permissions(path, fs::perms::none, fs::perm_options::replace);
  const std::ifstream probe(path, std::ios::binary);
  if (probe) {
    GTEST_SKIP() << "the current user can read files without permission";
  }
  const auto collector = OpenAndCollect(UrlFor(path));
  ASSERT_TRUE(collector->error_.has_value());
  EXPECT_EQ(collector->error_->code(), absl::StatusCode::kPermissionDenied);
}

TEST_F(FileProviderTest, InvalidUrlsAreRejectedSynchronously) {
  const auto bad = [&](const std::string& url) {
    ByteStreamRequest request;
    request.url = url;
    return provider_.Open(std::move(request), std::make_shared<Collector>())
        .status()
        .code();
  };
  EXPECT_EQ(bad("http://example.test/a"), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(bad("file://remote.example/a"), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(bad("file:///a%zz"), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(bad("file:///a%00b"), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(bad("file://"), absl::StatusCode::kInvalidArgument);
  ByteStreamRequest request;
  request.url = "file:///a";
  EXPECT_EQ(provider_.Open(std::move(request), nullptr).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST_F(FileProviderTest, PauseHoldsTheStreamAndResumeContinuesIt) {
  const auto bytes = Pattern(1000000, 5);
  const std::string url = UrlFor(WriteFile("big.bin", bytes));
  auto collector = std::make_shared<Collector>();
  ByteStreamRequest request;
  request.url = url;
  auto stream = provider_.Open(std::move(request), collector);
  ASSERT_TRUE(stream.ok());
  ASSERT_TRUE((*stream)->Pause().ok());
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  size_t held = 0;
  {
    const std::scoped_lock lock(collector->mutex_);
    held = collector->data_.size();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  {
    const std::scoped_lock lock(collector->mutex_);
    EXPECT_EQ(collector->data_.size(), held);
    EXPECT_FALSE(collector->ended_);
  }
  ASSERT_TRUE((*stream)->Resume().ok());
  ASSERT_TRUE(collector->WaitTerminal());
  EXPECT_EQ(collector->data_, bytes);
  EXPECT_FALSE((*stream)->Pause().ok());
}

TEST_F(FileProviderTest, CancelStopsDeliveryAndDestructionJoins) {
  const std::string url = UrlFor(WriteFile("big.bin", Pattern(4000000, 6)));
  auto collector = std::make_shared<Collector>();
  {
    ByteStreamRequest request;
    request.url = url;
    auto stream = provider_.Open(std::move(request), collector);
    ASSERT_TRUE(stream.ok());
    ASSERT_TRUE((*stream)->Pause().ok());
    (*stream)->Cancel();
    (*stream)->Cancel();
  }
  const std::scoped_lock lock(collector->mutex_);
  EXPECT_FALSE(collector->ended_);
}

class FileDataSourceTest : public FileProviderTest {
 protected:
  FileDataSourceTest()
      : executor_(umbra::base::GetDefaultTickClock()),
        runner_(executor_.CreateSequencedTaskRunner(
            umbra::base::TaskPriority::kControl)),
        redactor_(std::make_shared<umbra::base::UrlRedactor>()) {}

  bool RunUntil(const std::function<bool()>& done) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
      executor_.RunUntilIdle();
      if (done()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  }

  absl::StatusOr<std::shared_ptr<umbra::source::DataSource>> OpenSource(
      const std::string& url) {
    std::optional<absl::StatusOr<std::shared_ptr<umbra::source::DataSource>>>
        result;
    auto provider = std::make_shared<FileByteStreamProvider>();
    std::shared_ptr<umbra::source::OpenRequest> request;
    runner_->PostTask([&] {
      ByteStreamRequest stream_request;
      stream_request.url = url;
      request = umbra::source::ByteStreamDataSource::Open(
          provider, std::move(stream_request), runner_, redactor_, {},
          [&](absl::StatusOr<std::shared_ptr<umbra::source::DataSource>> r) {
            result = std::move(r);
          });
    });
    EXPECT_TRUE(RunUntil([&] { return result.has_value(); }));
    return result.has_value() ? *std::move(result)
                              : absl::InternalError("open did not finish");
  }

  absl::StatusOr<SharedBytes> ReadAt(umbra::source::DataSource& source,
                                     uint64_t offset, size_t size) {
    std::optional<absl::StatusOr<SharedBytes>> result;
    std::shared_ptr<umbra::source::ReadRequest> request;
    runner_->PostTask([&] {
      request = source.Read(offset, size, [&](absl::StatusOr<SharedBytes> r) {
        result = std::move(r);
      });
    });
    EXPECT_TRUE(RunUntil([&] { return result.has_value(); }));
    return result.has_value() ? *std::move(result)
                              : absl::InternalError("read did not finish");
  }

  umbra::base::SingleThreadTaskExecutor executor_;
  std::shared_ptr<umbra::base::SequencedTaskRunner> runner_;
  std::shared_ptr<umbra::base::UrlRedactor> redactor_;
};

TEST_F(FileDataSourceTest, SeeksAcrossAFourGibOffsetThroughTheDataSource) {
  const fs::path path = root_ / "sparse.bin";
  {
    const std::ofstream out(path, std::ios::binary);
  }
  fs::resize_file(path, kFourGiB + 100000);
  const std::vector<uint8_t> marker = Pattern(4096, 11);
  {
    std::fstream out(path, std::ios::binary | std::ios::in | std::ios::out);
    out.seekp(static_cast<std::streamoff>(kFourGiB + 8192));
    out.write(reinterpret_cast<const char*>(marker.data()),
              static_cast<std::streamsize>(marker.size()));
  }
  auto source = OpenSource(UrlFor(path));
  ASSERT_TRUE(source.ok());
  EXPECT_EQ((*source)->Info().size, kFourGiB + 100000);
  EXPECT_TRUE((*source)->Info().seekable);
  EXPECT_TRUE((*source)->Info().content_type.empty());
  const auto bytes = ReadAt(**source, kFourGiB + 8192, marker.size());
  ASSERT_TRUE(bytes.ok());
  EXPECT_EQ(std::vector<uint8_t>(bytes->span().begin(), bytes->span().end()),
            marker);
  const auto head = ReadAt(**source, 0, 16);
  ASSERT_TRUE(head.ok());
  EXPECT_EQ(head->size(), 16U);
  const auto end = ReadAt(**source, kFourGiB + 100000, 16);
  ASSERT_TRUE(end.ok());
  EXPECT_TRUE(end->empty());
  runner_->PostTask([&] { (*source)->Close(); });
  executor_.RunUntilIdle();
}

TEST_F(FileDataSourceTest, FileErrorsMapToDistinctCatalogueCodes) {
  const auto code_of = [&](const std::string& url) {
    auto source = OpenSource(url);
    EXPECT_FALSE(source.ok());
    const auto detail = umbra::base::GetErrorDetail(source.status());
    EXPECT_TRUE(detail.ok() && detail->has_value());
    return detail->value().code;
  };
  EXPECT_EQ(code_of(UrlFor(root_ / "missing.mkv")),
            umbra::base::ErrorCode::kSourceNotFound);
  EXPECT_EQ(code_of(UrlFor(root_)), umbra::base::ErrorCode::kSourceNotReadable);
  const fs::path path = WriteFile("locked.bin", Pattern(10, 1));
  fs::permissions(path, fs::perms::none, fs::perm_options::replace);
  const std::ifstream probe(path, std::ios::binary);
  if (!probe) {
    EXPECT_EQ(code_of(UrlFor(path)),
              umbra::base::ErrorCode::kSourceAccessDenied);
  }
}

TEST_F(FileDataSourceTest, TheSchemeRegistryResolvesFileUrlsToTheProvider) {
  auto registry = std::make_shared<umbra::source::SchemeRegistry>();
  ASSERT_TRUE(
      registry->RegisterByteStream(std::make_shared<FileByteStreamProvider>())
          .ok());
  const umbra::source::SourceResolver resolver(registry, redactor_);
  const auto resolved =
      resolver.Resolve(UrlFor(WriteFile("a b.mkv", Pattern(10, 1))));
  ASSERT_TRUE(resolved.ok());
  EXPECT_EQ(resolved->kind, umbra::source::ProviderKind::kByteStream);
  EXPECT_NE(resolved->byte_stream, nullptr);
  const auto srt = resolver.Resolve("file:///subs/a.srt");
  EXPECT_FALSE(srt.ok());
}

}
}
