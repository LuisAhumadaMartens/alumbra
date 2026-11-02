#ifndef TEST_PENUMBRA_REFERENCE_PLAYER_SCRIPTED_DECODER_H_
#define TEST_PENUMBRA_REFERENCE_PLAYER_SCRIPTED_DECODER_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/port_types.h"

namespace penumbra::test {

struct DecoderScript {
  absl::Status create_status;
  absl::Status write_status;
  std::optional<size_t> fail_write_call;
  bool emit_output = true;
  bool reject_config_change = false;
  absl::Status stop_status;
};

struct DecoderRecord {
  std::string name;
  StreamConfig config;
  SeekTicket created_ticket = 0;
  SeekTicket last_ticket = 0;
  std::vector<int64_t> written_pts;
  std::vector<size_t> write_sizes;
  size_t write_calls = 0;
  size_t flushes = 0;
  size_t end_of_streams = 0;
  size_t stops = 0;
  bool stopped = false;
  size_t grant = 0;
  DecoderScript script;
  std::shared_ptr<DecoderCallbacks> callbacks;
};

class ScriptedDecoder final : public Decoder {
 public:
  explicit ScriptedDecoder(std::shared_ptr<DecoderRecord> record);
  absl::Status WriteSamples(SeekTicket ticket,
                            std::span<const EncodedSample> samples) override;
  absl::Status EndOfStream(SeekTicket ticket) override;
  absl::Status Flush(SeekTicket ticket) override;
  absl::Status Stop() override;

 private:
  std::shared_ptr<DecoderRecord> record_;
};

class ScriptedFactory final : public ComponentFactory {
 public:
  void AddCandidate(DecoderCandidate candidate, DecoderScript script = {});
  void SetScript(const std::string& name, DecoderScript script);
  [[nodiscard]] absl::StatusOr<std::vector<DecoderCandidate>>
  GetDecoderCandidates(StreamType type) const override;
  absl::StatusOr<std::unique_ptr<Decoder>> CreateDecoder(
      const DecoderCandidate& candidate, StreamConfig config, SeekTicket ticket,
      std::shared_ptr<DecoderCallbacks> callbacks) override;
  absl::StatusOr<std::unique_ptr<VideoSink>> CreateVideoSink(
      OutputMode mode) override;
  absl::StatusOr<std::unique_ptr<AudioSink>> CreateAudioSink() override;

  absl::Status RequestData(const std::string& name, size_t count);
  absl::Status EmitError(const std::string& name, PortError error);
  absl::Status EmitFrame(const std::string& name, int64_t pts);
  [[nodiscard]] std::vector<std::shared_ptr<DecoderRecord>> Records(
      const std::string& name) const;
  [[nodiscard]] size_t CreationAttempts(const std::string& name) const;
  [[nodiscard]] const std::vector<std::string>& CreationOrder() const {
    return order_;
  }

 private:
  std::vector<DecoderCandidate> candidates_;
  std::map<std::string, DecoderScript> scripts_;
  std::map<std::string, std::vector<std::shared_ptr<DecoderRecord>>> records_;
  std::map<std::string, size_t> attempts_;
  std::vector<std::string> order_;
};

}

#endif
