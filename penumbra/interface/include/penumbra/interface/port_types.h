#ifndef PENUMBRA_INTERFACE_PORT_TYPES_H_
#define PENUMBRA_INTERFACE_PORT_TYPES_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/interface.h"

namespace penumbra {

using SeekTicket = uint64_t;

enum class OutputMode { kTexture, kPunchOut };
enum class InputMode { kSamples, kFragments };
enum class PlayerState { kReady, kPresenting, kEndOfStream };
enum class RecoveryHint { kTransient, kRecreate, kFatal };
enum class PortErrorCategory { kDecode, kOutput, kCapability, kPlatform };
enum class MemoryPressure { kModerate, kCritical };
enum class LogLevel { kVerbose, kDebug, kInfo, kWarning, kError };

struct OutputBounds {
  int32_t x = 0;
  int32_t y = 0;
  int32_t width = 0;
  int32_t height = 0;
  int32_t z_order = 0;
};

struct VideoCeiling {
  int32_t width = 0;
  int32_t height = 0;
  Rational frame_rate;
  int64_t bitrate = 0;
};

struct StreamConfig {
  StreamInfo info;
  std::optional<EncryptionScheme> encryption_scheme;
  OutputMode output_mode = OutputMode::kTexture;
};

struct PlayerConfig {
  int api_version = kApiVersion;
  SeekTicket initial_ticket = 1;
  std::vector<StreamConfig> streams;
  InputMode input_mode = InputMode::kSamples;
  std::optional<VideoCeiling> video_ceiling;
  DecoderSettings decoder;
};

struct MediaFragment {
  SharedBytes data;
  std::vector<uint32_t> track_ids;
  MediaTime presentation_time = MediaTime::Unknown();
  TimeDelta duration = TimeDelta::Unknown();
  bool initialization = false;
};

struct CapabilityRequest {
  StreamInfo stream;
  std::string mime_type;
  std::string key_system;
  std::optional<EncryptionScheme> encryption_scheme;
  std::string hdr_format;
  int64_t bitrate = 0;
};

struct DecodingCapability {
  bool supported = false;
  bool smooth = false;
  bool power_efficient = false;
  OutputMode preferred_output_mode = OutputMode::kTexture;
  bool accepts_fragments = false;
};

struct PortLimits {
  int api_version = kApiVersion;
  size_t max_players = 1;
};

struct FrameCounters {
  uint64_t decoded = 0;
  uint64_t dropped = 0;
  uint64_t corrupted = 0;
};

struct PlaybackSnapshot {
  MediaTime media_time = MediaTime::Unknown();
  TimeDelta audio_output_latency = TimeDelta::Unknown();
  FrameCounters frames;
};

}

#endif
