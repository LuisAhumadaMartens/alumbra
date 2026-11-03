#ifndef PENUMBRA_DESKTOP_OPENSSL_CRYPTO_PROVIDER_H_
#define PENUMBRA_DESKTOP_OPENSSL_CRYPTO_PROVIDER_H_

#include "penumbra/interface/crypto_provider.h"

namespace penumbra::desktop {

class OpenSslCryptoProvider final : public CryptoProvider {
 public:
  std::unique_ptr<Aes128CbcDecryptor> CreateAes128CbcDecryptor(
      std::span<const std::uint8_t> key,
      std::span<const std::uint8_t> iv) override;
};

}

#endif
