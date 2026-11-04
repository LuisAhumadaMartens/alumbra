#include "penumbra/desktop/openssl_crypto_provider.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"
#include "penumbra/interface/crypto_provider.h"

namespace penumbra::desktop {
namespace {

std::vector<std::uint8_t> Hex(std::string_view value) {
  std::vector<std::uint8_t> bytes;
  while (!value.empty()) {
    const auto digit = [](char character) -> std::uint8_t {
      if (character >= '0' && character <= '9') {
        return static_cast<std::uint8_t>(character - '0');
      }
      return static_cast<std::uint8_t>(character - 'a' + 10);
    };
    bytes.push_back(
        static_cast<std::uint8_t>((digit(value[0]) << 4) | digit(value[1])));
    value.remove_prefix(2);
  }
  return bytes;
}

TEST(OpenSslCryptoProviderTest, DecryptsNistSp80038aVectorWithPadding) {
  OpenSslCryptoProvider provider;
  const std::vector<std::uint8_t> key = Hex("2b7e151628aed2a6abf7158809cf4f3c");
  const std::vector<std::uint8_t> iv = Hex("000102030405060708090a0b0c0d0e0f");
  const std::vector<std::uint8_t> ciphertext =
      Hex("7649abac8119b246cee98e9b12e9197d"
          "8964e0b149c10b7b682e6e39aaeb731c");
  const std::vector<std::uint8_t> expected =
      Hex("6bc1bee22e409f96e93d7e117393172a");
  std::unique_ptr<Aes128CbcDecryptor> decryptor =
      provider.CreateAes128CbcDecryptor(key, iv);
  ASSERT_NE(decryptor, nullptr);
  CryptoResult first =
      decryptor->Update(std::span<const std::uint8_t>(ciphertext).first(16));
  ASSERT_TRUE(first.ok);
  CryptoResult second = decryptor->Update(
      std::span<const std::uint8_t>(ciphertext).subspan(16, 16));
  ASSERT_TRUE(second.ok);
  CryptoResult final = decryptor->Finalize();
  ASSERT_TRUE(final.ok);
  std::vector<std::uint8_t> plaintext;
  plaintext.insert(plaintext.end(), first.bytes.begin(), first.bytes.end());
  plaintext.insert(plaintext.end(), second.bytes.begin(), second.bytes.end());
  plaintext.insert(plaintext.end(), final.bytes.begin(), final.bytes.end());
  EXPECT_EQ(plaintext, expected);
}

TEST(OpenSslCryptoProviderTest, RejectsInvalidPadding) {
  OpenSslCryptoProvider provider;
  const std::vector<std::uint8_t> key = Hex("2b7e151628aed2a6abf7158809cf4f3c");
  const std::vector<std::uint8_t> iv = Hex("000102030405060708090a0b0c0d0e0f");
  std::vector<std::uint8_t> ciphertext =
      Hex("7649abac8119b246cee98e9b12e9197d"
          "8964e0b149c10b7b682e6e39aaeb731d");
  std::unique_ptr<Aes128CbcDecryptor> decryptor =
      provider.CreateAes128CbcDecryptor(key, iv);
  ASSERT_NE(decryptor, nullptr);
  const CryptoResult update = decryptor->Update(ciphertext);
  ASSERT_TRUE(update.ok);
  EXPECT_FALSE(decryptor->Finalize().ok);
}

TEST(OpenSslCryptoProviderTest, RejectsInvalidKeyAndIvSizes) {
  OpenSslCryptoProvider provider;
  const std::array<std::uint8_t, 15> short_value{};
  const std::array<std::uint8_t, 16> full_value{};
  EXPECT_EQ(provider.CreateAes128CbcDecryptor(short_value, full_value),
            nullptr);
  EXPECT_EQ(provider.CreateAes128CbcDecryptor(full_value, short_value),
            nullptr);
}

}
}
