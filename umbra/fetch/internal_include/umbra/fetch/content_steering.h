#ifndef UMBRA_FETCH_CONTENT_STEERING_H_
#define UMBRA_FETCH_CONTENT_STEERING_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "penumbra/interface/http_transport.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/base/task_runner.h"

namespace umbra::fetch {

inline constexpr int kSteeringManifestVersion = 1;

struct PathwayClone {
  std::string base_id;
  std::string id;
  std::string host;
  std::vector<std::pair<std::string, std::string>> query_parameters;
};

struct SteeringManifest {
  int version = kSteeringManifestVersion;
  absl::Duration ttl = absl::Seconds(300);
  std::string reload_uri;
  std::vector<std::string> pathway_priority;
  std::vector<PathwayClone> pathway_clones;
};

class SteeringParser {
 public:
  SteeringParser() = default;
  SteeringParser(const SteeringParser&) = delete;
  SteeringParser& operator=(const SteeringParser&) = delete;
  virtual ~SteeringParser() = default;

  [[nodiscard]] virtual absl::StatusOr<SteeringManifest> Parse(
      const penumbra::SharedBytes& bytes) = 0;
};

class JsonSteeringParser final : public SteeringParser {
 public:
  [[nodiscard]] absl::StatusOr<SteeringManifest> Parse(
      const penumbra::SharedBytes& bytes) override;
};

class SteeringListener {
 public:
  SteeringListener() = default;
  SteeringListener(const SteeringListener&) = delete;
  SteeringListener& operator=(const SteeringListener&) = delete;
  virtual ~SteeringListener() = default;

  virtual void OnSteeringManifest(const SteeringManifest& manifest) = 0;
  virtual void OnSteeringFailure(const absl::Status& status) = 0;
};

struct SteeringClientConfig {
  std::string uri;
  std::string pathway_id;
  std::optional<uint64_t> throughput_bps;
  absl::Duration failure_retry = absl::Seconds(300);
  absl::Duration min_ttl = absl::Seconds(1);
  absl::Duration connect_timeout = absl::Seconds(10);
  size_t max_body_bytes = size_t{1} << 20;
};

struct SteeringClientDependencies {
  std::shared_ptr<penumbra::HttpTransport> transport;
  std::shared_ptr<base::SequencedTaskRunner> runner;
  std::shared_ptr<SteeringParser> parser;
  std::shared_ptr<SteeringListener> listener;
};

[[nodiscard]] absl::Status ValidateSteeringManifest(
    const SteeringManifest& manifest);

class SteeringClient {
 public:
  static absl::StatusOr<std::unique_ptr<SteeringClient>> Create(
      SteeringClientDependencies dependencies, SteeringClientConfig config);

  ~SteeringClient();
  SteeringClient(const SteeringClient&) = delete;
  SteeringClient& operator=(const SteeringClient&) = delete;

  void Start();
  void Stop();

  [[nodiscard]] uint64_t PollCount() const;

 private:
  class Impl;

  explicit SteeringClient(std::shared_ptr<Impl> impl);

  std::shared_ptr<Impl> impl_;
};

}

#endif
