#include <cstdint>
#include <optional>
#include <vector>

#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/encryption_info.h"
#include "penumbra/interface/shared_bytes.h"

int main() {
  const penumbra::SharedBytes segment =
      penumbra::SharedBytes::Adopt(std::vector<uint8_t>(1024, 0xAB));
  const std::optional<penumbra::SharedBytes> payload = segment.Slice(16, 512);
  if (!payload.has_value() ||
      payload->data() != segment.span().subspan(16).data()) {
    return 1;
  }

  penumbra::EncodedSample sample;
  sample.track_id = 1;
  sample.data = *payload;
  sample.key_frame = true;
  sample.encryption = penumbra::EncryptionInfo{
      .scheme = penumbra::EncryptionScheme::kCbcs,
      .iv = std::vector<uint8_t>(16),
      .subsamples = {{.clear_bytes = 112, .encrypted_bytes = 400}},
      .pattern = penumbra::EncryptionPattern{.crypt_byte_block = 1,
                                             .skip_byte_block = 9},
  };

  return penumbra::ValidateEncryption(sample) ==
                 penumbra::EncryptionCheck::kValid
             ? 0
             : 1;
}
