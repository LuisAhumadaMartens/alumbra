#ifndef PENUMBRA_REFERENCE_PLAYER_AUDIO_CONVERTER_PROVIDER_H_
#define PENUMBRA_REFERENCE_PLAYER_AUDIO_CONVERTER_PROVIDER_H_

#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/port_types.h"

namespace penumbra {

class ReferenceAudioConverterProvider {
 public:
  virtual ~ReferenceAudioConverterProvider() = default;
  virtual absl::StatusOr<std::vector<float>> ConvertAudio(
      const StreamConfig& stream, const DecodedAudioBuffer& buffer) = 0;
  virtual absl::Status SetAudioVolume(double volume) = 0;
  virtual absl::Status SetAudioRate(double rate) = 0;
  virtual void ResetAudioConverters() = 0;
};

}

#endif
