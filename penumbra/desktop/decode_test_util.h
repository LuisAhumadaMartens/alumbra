#ifndef PENUMBRA_DESKTOP_DECODE_TEST_UTIL_H_
#define PENUMBRA_DESKTOP_DECODE_TEST_UTIL_H_

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra::desktop::decode_test {

struct ClipStream {
  StreamInfo info;
  std::vector<EncodedSample> samples;
};

std::string FixturePath(std::string_view name);

absl::StatusOr<std::vector<uint8_t>> ReadFixture(std::string_view name);

absl::StatusOr<std::vector<ClipStream>> ReadClip(
    std::span<const uint8_t> bytes);

absl::StatusOr<ClipStream> ReadFirstStream(std::string_view fixture,
                                           StreamType type);

std::string Md5Hex(std::span<const uint8_t> bytes);

std::string DigestVideoFrame(const DecodedVideoFrame& frame);

std::string DigestAudioBuffer(const DecodedAudioBuffer& buffer);

std::vector<uint8_t> InterleavedPcm(const DecodedAudioBuffer& buffer);

std::vector<std::string> ReadFrameMd5(std::string_view fixture);

struct ReferenceOutput {
  std::vector<DecodedVideoFrame> video;
  std::vector<DecodedAudioBuffer> audio;
};

absl::StatusOr<ReferenceOutput> ReferenceDecode(const ClipStream& stream);

class EventCollector final : public DecoderCallbacks {
 public:
  absl::Status OnDecoded(DecoderEvent event) override;
  std::optional<DecoderEvent> Next(std::chrono::milliseconds timeout);

 private:
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<DecoderEvent> events_;
};

struct DecodeOutput {
  std::vector<DecodedVideoFrame> video;
  std::vector<DecodedAudioBuffer> audio;
  std::vector<PortError> errors;
  std::vector<SeekTicket> frame_tickets;
  bool end_of_stream = false;
  bool timed_out = false;
};

struct DecodeRequest {
  Decoder* decoder = nullptr;
  EventCollector* events = nullptr;
  SeekTicket ticket = 1;
  std::span<const EncodedSample> samples;
  bool end_stream = true;
  size_t stop_after_writes = 0;
};

DecodeOutput RunDecode(const DecodeRequest& request);

class DeterministicRandom {
 public:
  explicit DeterministicRandom(uint64_t seed) : state_(seed) {}
  size_t Below(size_t limit);

 private:
  uint64_t state_;
};

void CorruptSamples(ClipStream& stream, DeterministicRandom& random);

size_t SecondKeyFrameIndex(const ClipStream& stream);

std::vector<int64_t> SortedPresentationMicros(const ClipStream& stream);

DecoderCandidate FindCandidate(ComponentFactory& factory, StreamType type,
                               std::string_view name);

}

#endif
