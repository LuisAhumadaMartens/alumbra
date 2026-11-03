#include "penumbra/desktop/openssl_crypto_provider.h"

#include <openssl/evp.h>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "penumbra/interface/crypto_provider.h"

namespace penumbra::desktop {
namespace {

class OpenSslAes128CbcDecryptor final : public Aes128CbcDecryptor {
 public:
  OpenSslAes128CbcDecryptor(const OpenSslAes128CbcDecryptor&) = delete;
  OpenSslAes128CbcDecryptor& operator=(const OpenSslAes128CbcDecryptor&) =
      delete;
  OpenSslAes128CbcDecryptor(OpenSslAes128CbcDecryptor&&) = delete;
  OpenSslAes128CbcDecryptor& operator=(OpenSslAes128CbcDecryptor&&) = delete;

  explicit OpenSslAes128CbcDecryptor(EVP_CIPHER_CTX* context)
      : context_(context) {}

  ~OpenSslAes128CbcDecryptor() override { EVP_CIPHER_CTX_free(context_); }

  CryptoResult Update(std::span<const std::uint8_t> ciphertext) override {
    if (finished_ || ciphertext.size() > static_cast<size_t>(INT_MAX)) {
      return {};
    }
    std::vector<std::uint8_t> output(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
    int output_size = 0;
    if (EVP_DecryptUpdate(context_, output.data(), &output_size,
                          ciphertext.data(),
                          static_cast<int>(ciphertext.size())) != 1) {
      finished_ = true;
      return {};
    }
    output.resize(static_cast<size_t>(output_size));
    return CryptoResult{.bytes = std::move(output), .ok = true};
  }

  CryptoResult Finalize() override {
    if (finished_) {
      return {};
    }
    finished_ = true;
    std::vector<std::uint8_t> output(EVP_MAX_BLOCK_LENGTH);
    int output_size = 0;
    if (EVP_DecryptFinal_ex(context_, output.data(), &output_size) != 1) {
      return {};
    }
    output.resize(static_cast<size_t>(output_size));
    return CryptoResult{.bytes = std::move(output), .ok = true};
  }

 private:
  EVP_CIPHER_CTX* context_ = nullptr;
  bool finished_ = false;
};

}

std::unique_ptr<Aes128CbcDecryptor>
OpenSslCryptoProvider::CreateAes128CbcDecryptor(
    std::span<const std::uint8_t> key, std::span<const std::uint8_t> iv) {
  if (key.size() != 16 || iv.size() != 16) {
    return nullptr;
  }
  EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
  if (context == nullptr) {
    return nullptr;
  }
  if (EVP_DecryptInit_ex(context, EVP_aes_128_cbc(), nullptr, key.data(),
                         iv.data()) != 1) {
    EVP_CIPHER_CTX_free(context);
    return nullptr;
  }
  return std::make_unique<OpenSslAes128CbcDecryptor>(context);
}

}
