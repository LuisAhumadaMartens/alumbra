#include "penumbra/desktop/file_byte_stream_provider.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_provider.h"

namespace penumbra::desktop {
namespace {

constexpr size_t kChunkBytes = 65536;

int HexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  return std::ranges::equal(a, b, [](char x, char y) {
    return std::tolower(static_cast<unsigned char>(x)) ==
           std::tolower(static_cast<unsigned char>(y));
  });
}

absl::Duration Now() {
  return absl::FromChrono(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()));
}

absl::Status StatusFromErrno(int error, std::string_view what) {
  if (error == ENOENT || error == ENOTDIR) {
    return absl::NotFoundError(absl::StrCat("no such file: ", what));
  }
  if (error == EACCES || error == EPERM) {
    return absl::PermissionDeniedError(
        absl::StrCat("permission denied: ", what));
  }
  return absl::UnavailableError(
      absl::StrCat("cannot read ", what, ": ",
                   std::error_code(error, std::generic_category()).message()));
}

class FileStream final : public ByteStream {
 public:
  FileStream(std::filesystem::path path, uint64_t offset,
             std::optional<uint64_t> length, std::string url,
             std::shared_ptr<ByteStreamCallbacks> callbacks)
      : path_(std::move(path)),
        offset_(offset),
        length_(length),
        url_(std::move(url)),
        callbacks_(std::move(callbacks)) {
    worker_ = std::thread([this] { Run(); });
  }

  FileStream(const FileStream&) = delete;
  FileStream& operator=(const FileStream&) = delete;

  ~FileStream() override {
    Cancel();
    worker_.join();
  }

  void Cancel() override {
    {
      const std::scoped_lock lock(mutex_);
      cancelled_ = true;
    }
    wake_.notify_all();
  }

  absl::Status Pause() override {
    const std::scoped_lock lock(mutex_);
    if (cancelled_ || finished_) {
      return absl::FailedPreconditionError("the stream is not active");
    }
    paused_ = true;
    return absl::OkStatus();
  }

  absl::Status Resume() override {
    {
      const std::scoped_lock lock(mutex_);
      if (cancelled_ || finished_) {
        return absl::FailedPreconditionError("the stream is not active");
      }
      paused_ = false;
    }
    wake_.notify_all();
    return absl::OkStatus();
  }

 private:
  bool WaitUntilReadable() {
    std::unique_lock lock(mutex_);
    wake_.wait(lock, [this] { return !paused_ || cancelled_; });
    return !cancelled_;
  }

  void Finish() {
    const std::scoped_lock lock(mutex_);
    finished_ = true;
  }

  void Fail(absl::Status error) {
    static_cast<void>(callbacks_->OnError(std::move(error)));
    Finish();
  }

  void Run() {
    std::error_code ec;
    const std::filesystem::file_status status =
        std::filesystem::status(path_, ec);
    if (ec) {
      Fail(StatusFromErrno(ec.value(), url_));
      return;
    }
    if (!std::filesystem::is_regular_file(status)) {
      Fail(absl::FailedPreconditionError(
          std::filesystem::is_directory(status)
              ? absl::StrCat("the path is a directory: ", url_)
              : absl::StrCat("the path is not a regular file: ", url_)));
      return;
    }
    const std::uintmax_t size = std::filesystem::file_size(path_, ec);
    if (ec) {
      Fail(StatusFromErrno(ec.value(), url_));
      return;
    }
    std::ifstream input(path_, std::ios::binary);
    if (!input) {
      Fail(StatusFromErrno(errno, url_));
      return;
    }
    ByteStreamInfo info;
    info.seekable = true;
    info.size = static_cast<uint64_t>(size);
    info.final_url = url_;
    if (!callbacks_->OnOpen(std::move(info)).ok()) {
      Finish();
      return;
    }
    uint64_t remaining = length_.value_or(UINT64_MAX);
    if (offset_ >= size) {
      remaining = 0;
    } else {
      input.seekg(static_cast<std::streamoff>(offset_));
      remaining = std::min<uint64_t>(remaining, size - offset_);
    }
    std::vector<uint8_t> buffer(kChunkBytes);
    while (remaining > 0) {
      if (!WaitUntilReadable()) {
        Finish();
        return;
      }
      const size_t want =
          static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
      input.read(reinterpret_cast<char*>(buffer.data()),
                 static_cast<std::streamsize>(want));
      const auto got = static_cast<size_t>(input.gcount());
      if (got == 0) {
        Fail(absl::UnavailableError(
            absl::StrCat("the file ended or failed while reading: ", url_)));
        return;
      }
      const absl::Duration arrival = Now();
      remaining -= got;
      ByteStreamChunk chunk;
      chunk.data =
          SharedBytes::Copy(std::span<const uint8_t>(buffer.data(), got));
      chunk.arrival_time = arrival;
      if (!callbacks_->OnData(std::move(chunk)).ok()) {
        Finish();
        return;
      }
    }
    if (!WaitUntilReadable()) {
      Finish();
      return;
    }
    static_cast<void>(callbacks_->OnEnd());
    Finish();
  }

  std::filesystem::path path_;
  uint64_t offset_;
  std::optional<uint64_t> length_;
  std::string url_;
  std::shared_ptr<ByteStreamCallbacks> callbacks_;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool paused_ = false;
  bool cancelled_ = false;
  bool finished_ = false;
  std::thread worker_;
};

}

absl::StatusOr<std::filesystem::path> PathFromFileUrl(std::string_view url) {
  constexpr std::string_view kPrefix = "file:";
  if (url.size() < kPrefix.size() ||
      !EqualsIgnoreCase(url.substr(0, kPrefix.size()), kPrefix)) {
    return absl::InvalidArgumentError("the URL does not use the file scheme");
  }
  std::string_view rest = url.substr(kPrefix.size());
  const size_t end = rest.find_first_of("?#");
  if (end != std::string_view::npos) {
    rest = rest.substr(0, end);
  }
  if (rest.starts_with("//")) {
    rest.remove_prefix(2);
    const size_t slash = rest.find('/');
    const std::string_view host = rest.substr(0, slash);
    if (!host.empty() && !EqualsIgnoreCase(host, "localhost")) {
      return absl::InvalidArgumentError(
          "file URLs with a remote host are not supported");
    }
    rest = slash == std::string_view::npos ? std::string_view()
                                           : rest.substr(slash);
  }
  if (rest.empty() || rest.front() != '/') {
    return absl::InvalidArgumentError("the file URL has no absolute path");
  }
  std::string decoded;
  decoded.reserve(rest.size());
  for (size_t i = 0; i < rest.size(); ++i) {
    if (rest[i] != '%') {
      decoded.push_back(rest[i]);
      continue;
    }
    if (i + 2 >= rest.size()) {
      return absl::InvalidArgumentError("malformed percent escape");
    }
    const int high = HexValue(rest[i + 1]);
    const int low = HexValue(rest[i + 2]);
    if (high < 0 || low < 0) {
      return absl::InvalidArgumentError("malformed percent escape");
    }
    decoded.push_back(static_cast<char>((high * 16) + low));
    i += 2;
  }
  if (decoded.find('\0') != std::string::npos) {
    return absl::InvalidArgumentError("the file path contains a NUL byte");
  }
  return std::filesystem::path(std::u8string(decoded.begin(), decoded.end()));
}

std::vector<std::string> FileByteStreamProvider::Schemes() const {
  return {"file"};
}

absl::StatusOr<std::unique_ptr<ByteStream>> FileByteStreamProvider::Open(
    ByteStreamRequest request, std::shared_ptr<ByteStreamCallbacks> callbacks) {
  if (callbacks == nullptr) {
    return absl::InvalidArgumentError("callbacks are required");
  }
  absl::StatusOr<std::filesystem::path> path = PathFromFileUrl(request.url);
  if (!path.ok()) {
    return path.status();
  }
  return std::make_unique<FileStream>(*std::move(path), request.offset,
                                      request.length, std::move(request.url),
                                      std::move(callbacks));
}

}
