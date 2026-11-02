#include "scripted_decoder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/player.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::test {
namespace {

DecodedVideoFrame MakeFrame(const StreamConfig& config, int64_t pts) {
  DecodedVideoFrame frame;
  auto storage = std::make_shared<std::array<uint8_t, 4>>();
  frame.storage = storage;
  frame.presentation_time = MediaTime::FromMicroseconds(pts);
  frame.format = PixelFormat::kI420;
  if (const auto* video = std::get_if<VideoInfo>(&config.info.format)) {
    frame.width = video->coded_width;
    frame.height = video->coded_height;
  }
  frame.planes[0] = {.data = storage->data(), .stride = 1};
  return frame;
}

int32_t CodedWidth(const StreamInfo& info) {
  const auto* video = std::get_if<VideoInfo>(&info.format);
  return video == nullptr ? 0 : video->coded_width;
}

}

ScriptedDecoder::ScriptedDecoder(std::shared_ptr<DecoderRecord> record)
    : record_(std::move(record)) {}

absl::Status ScriptedDecoder::WriteSamples(
    SeekTicket ticket, std::span<const EncodedSample> samples) {
  auto& record = *record_;
  if (record.stopped) {
    return absl::FailedPreconditionError("scripted decoder is stopped");
  }
  if (ticket != record.last_ticket || samples.empty() ||
      samples.size() > record.grant) {
    return absl::InvalidArgumentError("invalid scripted write");
  }
  ++record.write_calls;
  if (record.script.fail_write_call == record.write_calls) {
    return record.script.write_status;
  }
  for (const auto& sample : samples) {
    if (sample.config_change != nullptr && record.script.reject_config_change &&
        CodedWidth(*sample.config_change) != CodedWidth(record.config.info)) {
      return absl::UnimplementedError("cannot change type in place");
    }
  }
  record.grant = 0;
  record.write_sizes.push_back(samples.size());
  for (const auto& sample : samples) {
    if (sample.config_change != nullptr) {
      record.config.info = *sample.config_change;
    }
    record.written_pts.push_back(sample.pts.value);
    if (record.script.emit_output) {
      auto status = record.callbacks->OnDecoded(
          {.ticket = ticket,
           .detail = MakeFrame(record.config, sample.pts.value)});
      if (!status.ok()) {
        return status;
      }
    }
  }
  return absl::OkStatus();
}

absl::Status ScriptedDecoder::EndOfStream(SeekTicket ticket) {
  auto& record = *record_;
  if (record.grant == 0) {
    return absl::FailedPreconditionError("end of stream without a grant");
  }
  record.grant = 0;
  ++record.end_of_streams;
  if (!record.script.emit_output) {
    return absl::OkStatus();
  }
  return record.callbacks->OnDecoded(
      {.ticket = ticket,
       .detail = StateChanged{.state = PlayerState::kEndOfStream,
                              .track_id = record.config.info.track_id}});
}

absl::Status ScriptedDecoder::Flush(SeekTicket ticket) {
  auto& record = *record_;
  if (ticket <= record.last_ticket) {
    return absl::InvalidArgumentError("ticket must increase");
  }
  record.last_ticket = ticket;
  record.grant = 0;
  ++record.flushes;
  return absl::OkStatus();
}

absl::Status ScriptedDecoder::Stop() {
  auto& record = *record_;
  ++record.stops;
  if (!record.script.stop_status.ok()) {
    return record.script.stop_status;
  }
  record.stopped = true;
  record.grant = 0;
  record.callbacks.reset();
  return absl::OkStatus();
}

void ScriptedFactory::AddCandidate(DecoderCandidate candidate,
                                   DecoderScript script) {
  scripts_[candidate.name] = std::move(script);
  candidates_.push_back(std::move(candidate));
}

void ScriptedFactory::SetScript(const std::string& name, DecoderScript script) {
  scripts_[name] = std::move(script);
}

absl::StatusOr<std::vector<DecoderCandidate>>
ScriptedFactory::GetDecoderCandidates([[maybe_unused]] StreamType type) const {
  return candidates_;
}

absl::StatusOr<std::unique_ptr<Decoder>> ScriptedFactory::CreateDecoder(
    const DecoderCandidate& candidate, StreamConfig config, SeekTicket ticket,
    std::shared_ptr<DecoderCallbacks> callbacks) {
  ++attempts_[candidate.name];
  order_.push_back(candidate.name);
  const auto& script = scripts_[candidate.name];
  if (!script.create_status.ok()) {
    return script.create_status;
  }
  auto record = std::make_shared<DecoderRecord>();
  record->name = candidate.name;
  record->config = std::move(config);
  record->created_ticket = ticket;
  record->last_ticket = ticket;
  record->script = script;
  record->callbacks = std::move(callbacks);
  records_[candidate.name].push_back(record);
  return std::make_unique<ScriptedDecoder>(std::move(record));
}

absl::StatusOr<std::unique_ptr<VideoSink>> ScriptedFactory::CreateVideoSink(
    [[maybe_unused]] OutputMode mode) {
  return absl::UnimplementedError("scripted factory has no sinks");
}

absl::StatusOr<std::unique_ptr<AudioSink>> ScriptedFactory::CreateAudioSink() {
  return absl::UnimplementedError("scripted factory has no sinks");
}

absl::Status ScriptedFactory::RequestData(const std::string& name,
                                          size_t count) {
  const auto found = records_.find(name);
  if (found == records_.end() || found->second.empty()) {
    return absl::NotFoundError("no live scripted decoder");
  }
  auto& record = *found->second.back();
  if (record.stopped || record.callbacks == nullptr) {
    return absl::FailedPreconditionError("scripted decoder is stopped");
  }
  record.grant = count;
  return record.callbacks->OnDecoded(
      {.ticket = record.last_ticket,
       .detail = NeedsData{.track_id = record.config.info.track_id,
                           .max_samples = count}});
}

absl::Status ScriptedFactory::EmitError(const std::string& name,
                                        PortError error) {
  const auto found = records_.find(name);
  if (found == records_.end() || found->second.empty()) {
    return absl::NotFoundError("no live scripted decoder");
  }
  auto& record = *found->second.back();
  if (record.stopped || record.callbacks == nullptr) {
    return absl::FailedPreconditionError("scripted decoder is stopped");
  }
  return record.callbacks->OnDecoded(
      {.ticket = record.last_ticket, .detail = std::move(error)});
}

absl::Status ScriptedFactory::EmitFrame(const std::string& name, int64_t pts) {
  const auto found = records_.find(name);
  if (found == records_.end() || found->second.empty()) {
    return absl::NotFoundError("no live scripted decoder");
  }
  auto& record = *found->second.back();
  if (record.stopped || record.callbacks == nullptr) {
    return absl::FailedPreconditionError("scripted decoder is stopped");
  }
  return record.callbacks->OnDecoded(
      {.ticket = record.last_ticket, .detail = MakeFrame(record.config, pts)});
}

std::vector<std::shared_ptr<DecoderRecord>> ScriptedFactory::Records(
    const std::string& name) const {
  const auto found = records_.find(name);
  return found == records_.end() ? std::vector<std::shared_ptr<DecoderRecord>>{}
                                 : found->second;
}

size_t ScriptedFactory::CreationAttempts(const std::string& name) const {
  const auto found = attempts_.find(name);
  return found == attempts_.end() ? 0 : found->second;
}

}
