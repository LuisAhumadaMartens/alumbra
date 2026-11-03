#ifndef PENUMBRA_INTERFACE_CRYPTO_PROVIDER_H_
#define PENUMBRA_INTERFACE_CRYPTO_PROVIDER_H_

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace penumbra {

struct CryptoResult {
  std::vector<std::uint8_t> bytes;
  bool ok = false;
};

class Aes128CbcDecryptor {
 public:
  virtual ~Aes128CbcDecryptor() = default;
  virtual CryptoResult Update(std::span<const std::uint8_t> ciphertext) = 0;
  virtual CryptoResult Finalize() = 0;
};

class CryptoProvider {
 public:
  virtual ~CryptoProvider() = default;
  virtual std::unique_ptr<Aes128CbcDecryptor> CreateAes128CbcDecryptor(
      std::span<const std::uint8_t> key, std::span<const std::uint8_t> iv) = 0;
};

}

#endif
