#ifndef PENUMBRA_INTERFACE_PLAYER_H_
#define PENUMBRA_INTERFACE_PLAYER_H_

#include <memory>
#include <optional>
#include <span>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/port_types.h"

namespace penumbra {

struct NeedsData {
  uint32_t track_id = 0;
  size_t max_samples = 0;
};

struct StateChanged {
  PlayerState state = PlayerState::kReady;
  std::optional<uint32_t> track_id;
};

struct PortError {
  absl::Status status;
  PortErrorCategory category = PortErrorCategory::kPlatform;
  RecoveryHint recovery = RecoveryHint::kFatal;
};

struct PlayerEvent {
  SeekTicket ticket = 0;
  std::variant<NeedsData, StateChanged, PortError> detail;
};

class PlayerCallbacks {
 public:
  virtual ~PlayerCallbacks() = default;
  virtual absl::Status OnEvent(PlayerEvent event) = 0;
};

class Player {
 public:
  virtual ~Player() = default;
  virtual absl::Status WriteSamples(SeekTicket ticket, uint32_t track_id,
                                    std::span<const EncodedSample> samples) = 0;
  virtual absl::Status WriteFragment(SeekTicket ticket,
                                     uint32_t requested_track,
                                     const MediaFragment& fragment) = 0;
  virtual absl::Status EndOfStream(SeekTicket ticket, uint32_t track_id) = 0;
  virtual absl::Status Seek(SeekTicket ticket, MediaTime position) = 0;
  virtual absl::Status Flush(SeekTicket ticket) = 0;
  virtual absl::Status SetPlaybackRate(double rate) = 0;
  virtual absl::Status SetVolume(double volume) = 0;
  virtual absl::Status SetBounds(uint32_t track_id, OutputBounds bounds) = 0;
  virtual absl::StatusOr<PlaybackSnapshot> GetSnapshot() const = 0;
  virtual absl::StatusOr<std::optional<DecodedVideoFrame>> GetCurrentFrame(
      uint32_t track_id) const = 0;
  virtual absl::Status Stop() = 0;
};

class PlayerFactory {
 public:
  virtual ~PlayerFactory() = default;
  virtual absl::StatusOr<PortLimits> GetLimits() const = 0;
  virtual absl::StatusOr<std::unique_ptr<Player>> CreatePlayer(
      PlayerConfig config, std::shared_ptr<PlayerCallbacks> callbacks) = 0;
};

class CapabilityProvider {
 public:
  virtual ~CapabilityProvider() = default;
  virtual absl::StatusOr<DecodingCapability> QueryDecoding(
      const CapabilityRequest& request) const = 0;
  virtual absl::StatusOr<bool> CanChangeType(const StreamConfig& from,
                                             const StreamConfig& to) const = 0;
};

}

#endif
