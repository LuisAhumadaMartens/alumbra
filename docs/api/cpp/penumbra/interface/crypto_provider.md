\struct penumbra::CryptoResult
The output and success state of one incremental AES-CBC operation.

\var penumbra::CryptoResult::bytes
Decrypted bytes produced by the operation.

\var penumbra::CryptoResult::ok
Whether the operation succeeded.

\class penumbra::Aes128CbcDecryptor
Incremental AES-128-CBC decryption with PKCS#7 padding validation.

\fn virtual penumbra::CryptoResult penumbra::Aes128CbcDecryptor::Update(std::span<const std::uint8_t> ciphertext)
Decrypts ciphertext blocks and returns plaintext that is safe to consume before final padding validation. Callers must not expose the final media until `Finalize` succeeds.

\fn virtual penumbra::CryptoResult penumbra::Aes128CbcDecryptor::Finalize()
Validates and removes PKCS#7 padding, returning the remaining plaintext or an error.

\class penumbra::CryptoProvider
Optional platform capability for AES-128-CBC decryption.

\fn virtual std::unique_ptr<penumbra::Aes128CbcDecryptor> penumbra::CryptoProvider::CreateAes128CbcDecryptor(std::span<const std::uint8_t> key, std::span<const std::uint8_t> iv)
Creates a decryptor for a 16-octet key and 16-octet IV. Unsupported ports return `Unimplemented`.
