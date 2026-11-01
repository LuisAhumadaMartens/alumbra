#ifndef PENUMBRA_INTERFACE_ENCODED_SAMPLE_H_
#define PENUMBRA_INTERFACE_ENCODED_SAMPLE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "penumbra/interface/encryption_info.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra {

struct EncodedSample {
  uint32_t track_id = 0;
  SharedBytes data;
  Timestamp pts;
  Timestamp dts;
  Timestamp duration;
  bool key_frame = false;
  bool discontinuity = false;
  std::shared_ptr<const StreamInfo> config_change;
  Timestamp trim_start;
  Timestamp trim_end;
  SharedBytes hdr_dynamic_metadata;
  std::optional<EncryptionInfo> encryption;

  [[nodiscard]] std::string ToString() const {
    std::string text = "track=" + std::to_string(track_id) +
                       " pts=" + pts.ToString() + " dts=" + dts.ToString() +
                       " duration=" + duration.ToString() +
                       " size=" + std::to_string(data.size());
    if (key_frame) {
      text += " key";
    }
    if (discontinuity) {
      text += " discontinuity";
    }
    if (config_change != nullptr) {
      text += " config-change";
    }
    if (encryption.has_value()) {
      text += encryption->scheme == EncryptionScheme::kCbcs ? " cbcs" : " cenc";
    }
    return text;
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const EncodedSample& sample) {
    sink.Append(sample.ToString());
  }
};

inline EncryptionCheck ValidateEncryption(const EncodedSample& sample) {
  if (!sample.encryption.has_value()) {
    return EncryptionCheck::kValid;
  }
  return ValidateEncryption(*sample.encryption, sample.data.size());
}

}

#endif
