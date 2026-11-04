#include "umbra/fetch/content_steering.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/base/sequence_checker.h"
#include "umbra/base/task_runner.h"

namespace umbra::fetch {

namespace {

struct Json {
  using Object = std::vector<std::pair<std::string, Json>>;
  using Array = std::vector<Json>;
  std::variant<std::nullptr_t, int64_t, std::string, Object, Array> value;
};

class JsonParser {
 public:
  explicit JsonParser(std::string_view input) : input_(input) {}

  absl::StatusOr<Json> Parse() {
    auto value = Read(0);
    Space();
    if (!value.ok() || position_ != input_.size()) {
      return absl::InvalidArgumentError("invalid steering JSON");
    }
    return value;
  }

 private:
  void Space() {
    while (position_ < input_.size() &&
           (input_[position_] == ' ' || input_[position_] == '\n' ||
            input_[position_] == '\r' || input_[position_] == '\t')) {
      ++position_;
    }
  }

  bool Take(char value) {
    Space();
    if (position_ < input_.size() && input_[position_] == value) {
      ++position_;
      return true;
    }
    return false;
  }

  absl::StatusOr<std::string> String() {
    if (position_ >= input_.size()) {
      return absl::InvalidArgumentError("invalid steering JSON string");
    }
    const char opening = input_[position_++];
    if (opening != '"') {
      return absl::InvalidArgumentError("invalid steering JSON string");
    }
    std::string result;
    while (position_ < input_.size()) {
      const char value = input_[position_++];
      if (value == '"') {
        return result;
      }
      if (value == '\\') {
        absl::Status status = ReadEscape(result);
        if (!status.ok()) {
          return status;
        }
      } else if (static_cast<unsigned char>(value) < 0x20) {
        return absl::InvalidArgumentError("invalid steering JSON string");
      } else {
        result.push_back(value);
      }
    }
    return absl::InvalidArgumentError("invalid steering JSON string");
  }

  absl::Status ReadEscape(std::string& result) {
    if (position_ == input_.size()) {
      return absl::InvalidArgumentError("invalid steering JSON escape");
    }
    const char escaped = input_[position_++];
    switch (escaped) {
      case '"':
      case '\\':
      case '/':
        result.push_back(escaped);
        return absl::OkStatus();
      case 'n':
        result.push_back('\n');
        return absl::OkStatus();
      case 'r':
        result.push_back('\r');
        return absl::OkStatus();
      case 't':
        result.push_back('\t');
        return absl::OkStatus();
      case 'b':
        result.push_back('\b');
        return absl::OkStatus();
      case 'f':
        result.push_back('\f');
        return absl::OkStatus();
      case 'u':
        return ReadUnicodeEscape(result);
      default:
        return absl::InvalidArgumentError("invalid steering JSON escape");
    }
  }

  absl::Status ReadUnicodeEscape(std::string& result) {
    auto codepoint = CodeUnit();
    if (!codepoint.has_value()) {
      return absl::InvalidArgumentError("invalid steering JSON escape");
    }
    uint32_t value = *codepoint;
    if (value >= 0xd800 && value <= 0xdbff) {
      if (position_ + 2 > input_.size() || input_[position_] != '\\' ||
          input_[position_ + 1] != 'u') {
        return absl::InvalidArgumentError("invalid steering JSON escape");
      }
      position_ += 2;
      auto low = CodeUnit();
      if (!low.has_value() || *low < 0xdc00 || *low > 0xdfff) {
        return absl::InvalidArgumentError("invalid steering JSON escape");
      }
      value = 0x10000 + ((value - 0xd800) << 10) + (*low - 0xdc00);
    } else if (value >= 0xdc00 && value <= 0xdfff) {
      return absl::InvalidArgumentError("invalid steering JSON escape");
    }
    AppendUtf8(value, result);
    return absl::OkStatus();
  }

  std::optional<uint32_t> CodeUnit() {
    if (position_ + 4 > input_.size()) {
      return std::nullopt;
    }
    uint32_t value = 0;
    for (size_t index = 0; index < 4; ++index) {
      const char digit = input_[position_++];
      value <<= 4;
      if (digit >= '0' && digit <= '9') {
        value += static_cast<uint32_t>(digit - '0');
      } else if (digit >= 'a' && digit <= 'f') {
        value += static_cast<uint32_t>(digit - 'a' + 10);
      } else if (digit >= 'A' && digit <= 'F') {
        value += static_cast<uint32_t>(digit - 'A' + 10);
      } else {
        return std::nullopt;
      }
    }
    return value;
  }

  static void AppendUtf8(uint32_t value, std::string& output) {
    if (value <= 0x7f) {
      output.push_back(static_cast<char>(value));
    } else if (value <= 0x7ff) {
      output.push_back(static_cast<char>(0xc0 | (value >> 6)));
      output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else if (value <= 0xffff) {
      output.push_back(static_cast<char>(0xe0 | (value >> 12)));
      output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else {
      output.push_back(static_cast<char>(0xf0 | (value >> 18)));
      output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
      output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
  }

  absl::StatusOr<Json> Read(size_t depth) {
    Space();
    if (depth > 32 || position_ == input_.size()) {
      return absl::InvalidArgumentError("invalid steering JSON");
    }
    if (Take('{')) {
      return ReadObject(depth);
    }
    if (Take('[')) {
      return ReadArray(depth);
    }
    if (input_[position_] == '"') {
      auto value = String();
      if (!value.ok()) {
        return value.status();
      }
      return Json{std::move(*value)};
    }
    if (ReadLiteral("null") || ReadLiteral("true")) {
      return Json{nullptr};
    }
    if (ReadLiteral("false")) {
      return Json{nullptr};
    }
    return ReadNumber();
  }

  absl::StatusOr<Json> ReadObject(size_t depth) {
    Json::Object object;
    if (Take('}')) {
      return Json{std::move(object)};
    }
    for (size_t index = 0; index < 4096; ++index) {
      auto key = String();
      if (!key.ok() || !Take(':')) {
        return absl::InvalidArgumentError("invalid steering JSON object");
      }
      if (std::ranges::any_of(object, [&key](const auto& entry) {
            return entry.first == *key;
          })) {
        return absl::InvalidArgumentError("duplicate steering JSON key");
      }
      auto value = Read(depth + 1);
      if (!value.ok()) {
        return value.status();
      }
      object.emplace_back(std::move(*key), std::move(*value));
      if (Take('}')) {
        return Json{std::move(object)};
      }
      if (!Take(',')) {
        break;
      }
    }
    return absl::InvalidArgumentError("invalid steering JSON object");
  }

  absl::StatusOr<Json> ReadArray(size_t depth) {
    Json::Array array;
    if (Take(']')) {
      return Json{std::move(array)};
    }
    for (size_t index = 0; index < 4096; ++index) {
      auto value = Read(depth + 1);
      if (!value.ok()) {
        return value.status();
      }
      array.push_back(std::move(*value));
      if (Take(']')) {
        return Json{std::move(array)};
      }
      if (!Take(',')) {
        break;
      }
    }
    return absl::InvalidArgumentError("invalid steering JSON array");
  }

  bool ReadLiteral(std::string_view literal) {
    if (input_.substr(position_, literal.size()) != literal) {
      return false;
    }
    position_ += literal.size();
    return true;
  }

  bool ReadDigits() {
    const size_t start = position_;
    while (position_ < input_.size() && input_[position_] >= '0' &&
           input_[position_] <= '9') {
      ++position_;
    }
    return position_ != start;
  }

  absl::StatusOr<Json> ReadNumber() {
    const size_t start = position_;
    if (input_[position_] == '-') {
      ++position_;
    }
    if (position_ < input_.size() && input_[position_] == '0') {
      ++position_;
    } else if (!ReadDigits()) {
      return absl::InvalidArgumentError("invalid steering JSON number");
    }
    bool integer = true;
    if (position_ < input_.size() && input_[position_] == '.') {
      integer = false;
      ++position_;
      if (!ReadDigits()) {
        return absl::InvalidArgumentError("invalid steering JSON number");
      }
    }
    if (position_ < input_.size() &&
        (input_[position_] == 'e' || input_[position_] == 'E')) {
      integer = false;
      ++position_;
      if (position_ < input_.size() &&
          (input_[position_] == '+' || input_[position_] == '-')) {
        ++position_;
      }
      if (!ReadDigits()) {
        return absl::InvalidArgumentError("invalid steering JSON number");
      }
    }
    if (!integer) {
      return Json{nullptr};
    }
    int64_t number = 0;
    const std::string_view number_text =
        input_.substr(start, position_ - start);
    const auto parsed = std::from_chars(
        number_text.data(), number_text.data() + number_text.size(), number);
    if (position_ != start && parsed.ec == std::errc{} &&
        parsed.ptr == number_text.end()) {
      return Json{number};
    }
    return absl::InvalidArgumentError("invalid steering JSON value");
  }

  std::string_view input_;
  size_t position_ = 0;
};

const Json* Member(const Json::Object& object, std::string_view key) {
  const auto found = std::ranges::find_if(
      object, [key](const auto& entry) { return entry.first == key; });
  return found == object.end() ? nullptr : &found->second;
}

absl::StatusOr<std::string> StringMember(const Json::Object& object,
                                         std::string_view key, bool required) {
  const Json* value = Member(object, key);
  if (value == nullptr && !required) {
    return std::string();
  }
  if (value == nullptr || !std::holds_alternative<std::string>(value->value)) {
    return absl::InvalidArgumentError("invalid steering JSON string field");
  }
  return std::get<std::string>(value->value);
}

absl::Status ParsePriority(const Json::Object& object,
                           SteeringManifest& result) {
  const Json* priority = Member(object, "PATHWAY-PRIORITY");
  if (priority == nullptr ||
      !std::holds_alternative<Json::Array>(priority->value)) {
    return absl::InvalidArgumentError("invalid steering pathway priority");
  }
  for (const Json& entry : std::get<Json::Array>(priority->value)) {
    if (!std::holds_alternative<std::string>(entry.value)) {
      return absl::InvalidArgumentError("invalid steering pathway priority");
    }
    result.pathway_priority.push_back(std::get<std::string>(entry.value));
  }
  return absl::OkStatus();
}

absl::StatusOr<PathwayClone> ParseClone(const Json& entry) {
  if (!std::holds_alternative<Json::Object>(entry.value)) {
    return absl::InvalidArgumentError("invalid steering pathway clone");
  }
  const auto& clone_fields = std::get<Json::Object>(entry.value);
  auto base = StringMember(clone_fields, "BASE-ID", true);
  auto id = StringMember(clone_fields, "ID", true);
  const Json* replacement = Member(clone_fields, "URI-REPLACEMENT");
  if (!base.ok() || !id.ok() || replacement == nullptr ||
      !std::holds_alternative<Json::Object>(replacement->value)) {
    return absl::InvalidArgumentError("invalid steering pathway clone");
  }
  const auto& fields = std::get<Json::Object>(replacement->value);
  auto host = StringMember(fields, "HOST", false);
  if (!host.ok()) {
    return host.status();
  }
  PathwayClone clone{.base_id = std::move(*base),
                     .id = std::move(*id),
                     .host = std::move(*host),
                     .query_parameters = {}};
  const Json* params = Member(fields, "PARAMS");
  if (params == nullptr) {
    return clone;
  }
  if (!std::holds_alternative<Json::Object>(params->value)) {
    return absl::InvalidArgumentError("invalid steering clone params");
  }
  for (const auto& [key, value] : std::get<Json::Object>(params->value)) {
    if (!std::holds_alternative<std::string>(value.value)) {
      return absl::InvalidArgumentError("invalid steering clone params");
    }
    clone.query_parameters.emplace_back(key,
                                        std::get<std::string>(value.value));
  }
  return clone;
}

absl::Status ParseClones(const Json::Object& object, SteeringManifest& result) {
  const Json* clones = Member(object, "PATHWAY-CLONES");
  if (clones == nullptr) {
    return absl::OkStatus();
  }
  if (!std::holds_alternative<Json::Array>(clones->value)) {
    return absl::InvalidArgumentError("invalid steering pathway clones");
  }
  for (const Json& entry : std::get<Json::Array>(clones->value)) {
    auto clone = ParseClone(entry);
    if (!clone.ok()) {
      return clone.status();
    }
    result.pathway_clones.push_back(std::move(*clone));
  }
  return absl::OkStatus();
}

absl::StatusOr<SteeringManifest> ParseManifestObject(
    const Json::Object& object) {
  const Json* version = Member(object, "VERSION");
  const Json* ttl = Member(object, "TTL");
  if (version == nullptr || ttl == nullptr ||
      !std::holds_alternative<int64_t>(version->value) ||
      !std::holds_alternative<int64_t>(ttl->value)) {
    return absl::InvalidArgumentError("invalid steering JSON fields");
  }
  const int64_t version_number = std::get<int64_t>(version->value);
  const int64_t ttl_seconds = std::get<int64_t>(ttl->value);
  if (version_number > std::numeric_limits<int>::max() || ttl_seconds < 0) {
    return absl::InvalidArgumentError("invalid steering JSON values");
  }
  SteeringManifest result;
  result.version = static_cast<int>(version_number);
  result.ttl = absl::Seconds(ttl_seconds);
  auto reload = StringMember(object, "RELOAD-URI", false);
  if (!reload.ok()) {
    return reload.status();
  }
  result.reload_uri = std::move(*reload);
  if (absl::Status status = ParsePriority(object, result); !status.ok()) {
    return status;
  }
  if (absl::Status status = ParseClones(object, result); !status.ok()) {
    return status;
  }
  if (absl::Status status = ValidateSteeringManifest(result); !status.ok()) {
    return status;
  }
  return result;
}

}

absl::StatusOr<SteeringManifest> JsonSteeringParser::Parse(
    const penumbra::SharedBytes& bytes) {
  if (bytes.size() > (size_t{1} << 20)) {
    return absl::ResourceExhaustedError("steering manifest is too large");
  }
  const auto data = bytes.span();
  auto parsed =
      JsonParser(std::string_view(reinterpret_cast<const char*>(data.data()),
                                  data.size()))
          .Parse();
  if (!parsed.ok() || !std::holds_alternative<Json::Object>(parsed->value)) {
    return absl::InvalidArgumentError("invalid steering JSON object");
  }
  return ParseManifestObject(std::get<Json::Object>(parsed->value));
}

absl::Status ValidateSteeringManifest(const SteeringManifest& manifest) {
  if (manifest.version != kSteeringManifestVersion) {
    return absl::InvalidArgumentError(absl::StrCat(
        "unsupported steering manifest version ", manifest.version));
  }
  if (manifest.ttl < absl::ZeroDuration()) {
    return absl::InvalidArgumentError("steering TTL must not be negative");
  }
  std::set<std::string> seen;
  for (const std::string& pathway : manifest.pathway_priority) {
    if (pathway.empty() || !seen.insert(pathway).second) {
      return absl::InvalidArgumentError(
          "steering priority needs distinct nonempty pathway identifiers");
    }
  }
  return absl::OkStatus();
}

class SteeringClient::Impl : public std::enable_shared_from_this<Impl> {
 public:
  Impl(SteeringClientDependencies dependencies, SteeringClientConfig config)
      : deps_(std::move(dependencies)),
        config_(std::move(config)),
        uri_(config_.uri) {}

  void Start();
  void Stop();
  void Poll(uint64_t epoch);
  void HandleResult(uint64_t epoch, const absl::Status& status,
                    const penumbra::SharedBytes& body);

  [[nodiscard]] uint64_t PollCount() const { return poll_count_; }

 private:
  class Callbacks;

  void Schedule(absl::Duration delay);
  void Fail(const absl::Status& status);
  [[nodiscard]] std::string RequestUri() const;

  SteeringClientDependencies deps_;
  SteeringClientConfig config_;
  std::string uri_;
  uint64_t epoch_ = 0;
  uint64_t poll_count_ = 0;
  bool running_ = false;
  std::unique_ptr<penumbra::HttpRequestHandle> handle_;
  base::SequenceChecker sequence_;
};

class SteeringClient::Impl::Callbacks final
    : public penumbra::HttpTransportCallbacks {
 public:
  Callbacks(std::weak_ptr<Impl> impl,
            std::shared_ptr<base::SequencedTaskRunner> runner, uint64_t epoch,
            size_t max_body_bytes)
      : impl_(std::move(impl)),
        runner_(std::move(runner)),
        epoch_(epoch),
        max_body_bytes_(max_body_bytes) {}

  absl::Status OnResponse(penumbra::HttpResponseHead head) override {
    http_status_ = head.status;
    return absl::OkStatus();
  }

  absl::Status OnChunk(penumbra::HttpChunk chunk) override {
    const size_t size = chunk.data.size();
    if (body_.size() + size > max_body_bytes_) {
      overflow_ = true;
      return absl::OkStatus();
    }
    const std::span<const uint8_t> bytes = chunk.data.span();
    body_.insert(body_.end(), bytes.begin(), bytes.end());
    return absl::OkStatus();
  }

  absl::Status OnComplete(
      [[maybe_unused]] penumbra::HttpMetrics metrics) override {
    absl::Status status = absl::OkStatus();
    if (overflow_) {
      status = absl::ResourceExhaustedError("steering manifest is too large");
    } else if (http_status_ < 200 || http_status_ > 299) {
      status = absl::UnavailableError(
          absl::StrCat("steering manifest HTTP status ", http_status_));
    }
    Post(std::move(status), penumbra::SharedBytes::Adopt(std::move(body_)));
    return absl::OkStatus();
  }

  absl::Status OnError(absl::Status error) override {
    Post(std::move(error), penumbra::SharedBytes());
    return absl::OkStatus();
  }

 private:
  void Post(absl::Status status, penumbra::SharedBytes body) {
    runner_->PostTask([impl = impl_, epoch = epoch_, status = std::move(status),
                       body = std::move(body)]() mutable {
      if (const std::shared_ptr<Impl> target = impl.lock()) {
        target->HandleResult(epoch, status, body);
      }
    });
  }

  std::weak_ptr<Impl> impl_;
  std::shared_ptr<base::SequencedTaskRunner> runner_;
  uint64_t epoch_;
  size_t max_body_bytes_;
  int http_status_ = 0;
  bool overflow_ = false;
  std::vector<uint8_t> body_;
};

void SteeringClient::Impl::Start() {
  assert(sequence_.CalledOnValidSequence());
  if (running_) {
    return;
  }
  running_ = true;
  ++epoch_;
  Poll(epoch_);
}

void SteeringClient::Impl::Stop() {
  assert(sequence_.CalledOnValidSequence());
  running_ = false;
  ++epoch_;
  if (handle_ != nullptr) {
    handle_->Cancel();
    handle_.reset();
  }
}

void SteeringClient::Impl::Schedule(absl::Duration delay) {
  const uint64_t epoch = epoch_;
  deps_.runner->PostDelayedTask(
      [weak = weak_from_this(), epoch] {
        if (const std::shared_ptr<Impl> target = weak.lock()) {
          target->Poll(epoch);
        }
      },
      delay);
}

void SteeringClient::Impl::Poll(uint64_t epoch) {
  assert(sequence_.CalledOnValidSequence());
  if (!running_ || epoch != epoch_) {
    return;
  }
  ++poll_count_;
  penumbra::HttpRequest request;
  request.url = RequestUri();
  request.request_class = penumbra::RequestClass::kManifest;
  request.priority = penumbra::RequestPriority::kHigh;
  request.connect_timeout = config_.connect_timeout;
  auto callbacks = std::make_shared<Callbacks>(weak_from_this(), deps_.runner,
                                               epoch_, config_.max_body_bytes);
  auto started = deps_.transport->Start(std::move(request), callbacks);
  if (!started.ok()) {
    Fail(started.status());
    return;
  }
  handle_ = *std::move(started);
}

std::string SteeringClient::Impl::RequestUri() const {
  std::string result = uri_;
  const size_t fragment_position = result.find('#');
  const std::string fragment = fragment_position == std::string::npos
                                   ? std::string()
                                   : result.substr(fragment_position);
  if (fragment_position != std::string::npos) {
    result.resize(fragment_position);
  }
  const auto append = [&result](std::string_view key, std::string_view value) {
    result.push_back(result.find('?') == std::string::npos ? '?' : '&');
    result.append(key);
    result.push_back('=');
    for (const unsigned char character : value) {
      if ((character >= 'a' && character <= 'z') ||
          (character >= 'A' && character <= 'Z') ||
          (character >= '0' && character <= '9') || character == '-' ||
          character == '_' || character == '.' || character == '~') {
        result.push_back(static_cast<char>(character));
      } else {
        constexpr std::array<char, 16> kHex = {'0', '1', '2', '3', '4', '5',
                                               '6', '7', '8', '9', 'A', 'B',
                                               'C', 'D', 'E', 'F'};
        result.push_back('%');
        result.push_back(kHex.at(character >> 4));
        result.push_back(kHex.at(character & 0x0f));
      }
    }
  };
  if (!config_.pathway_id.empty()) {
    append("_HLS_pathway", config_.pathway_id);
  }
  if (config_.throughput_bps.has_value()) {
    const std::string value = std::to_string(*config_.throughput_bps);
    append("_HLS_throughput", value);
  }
  result.append(fragment);
  return result;
}

void SteeringClient::Impl::Fail(const absl::Status& status) {
  handle_.reset();
  deps_.listener->OnSteeringFailure(status);
  Schedule(config_.failure_retry);
}

void SteeringClient::Impl::HandleResult(uint64_t epoch,
                                        const absl::Status& status,
                                        const penumbra::SharedBytes& body) {
  assert(sequence_.CalledOnValidSequence());
  if (!running_ || epoch != epoch_) {
    return;
  }
  handle_.reset();
  if (!status.ok()) {
    Fail(status);
    return;
  }
  absl::StatusOr<SteeringManifest> parsed = deps_.parser->Parse(body);
  if (parsed.ok()) {
    if (absl::Status valid = ValidateSteeringManifest(*parsed); !valid.ok()) {
      parsed = std::move(valid);
    }
  }
  if (!parsed.ok()) {
    Fail(parsed.status());
    return;
  }
  if (!parsed->reload_uri.empty()) {
    uri_ = parsed->reload_uri;
  }
  const absl::Duration ttl = std::max(parsed->ttl, config_.min_ttl);
  deps_.listener->OnSteeringManifest(*parsed);
  Schedule(ttl);
}

absl::StatusOr<std::unique_ptr<SteeringClient>> SteeringClient::Create(
    SteeringClientDependencies dependencies, SteeringClientConfig config) {
  if (dependencies.transport == nullptr || dependencies.runner == nullptr ||
      dependencies.parser == nullptr || dependencies.listener == nullptr) {
    return absl::InvalidArgumentError(
        "transport, runner, parser and listener are required");
  }
  if (config.uri.empty()) {
    return absl::InvalidArgumentError("a steering URI is required");
  }
  if (config.min_ttl <= absl::ZeroDuration() ||
      config.failure_retry <= absl::ZeroDuration() ||
      config.max_body_bytes == 0) {
    return absl::InvalidArgumentError(
        "steering timings and body limit must be positive");
  }
  auto impl =
      std::make_shared<Impl>(std::move(dependencies), std::move(config));
  return std::unique_ptr<SteeringClient>(new SteeringClient(std::move(impl)));
}

SteeringClient::SteeringClient(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

SteeringClient::~SteeringClient() { impl_->Stop(); }

void SteeringClient::Start() { impl_->Start(); }

void SteeringClient::Stop() { impl_->Stop(); }

uint64_t SteeringClient::PollCount() const { return impl_->PollCount(); }

}
