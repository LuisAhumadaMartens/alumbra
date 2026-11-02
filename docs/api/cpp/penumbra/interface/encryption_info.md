\file penumbra/interface/encryption_info.h
Common Encryption (ISO/IEC 23001-7) metadata carried by each encrypted sample.

The metadata is part of the sample from the first contract version, so adding DRM does not change the sample
type. Where a content decryption module attaches is decided by D-27.

\enum penumbra::EncryptionScheme
Common Encryption protection scheme.

\var penumbra::EncryptionScheme::kCenc
The `cenc` scheme: AES-CTR encryption of whole samples or subsamples.

\var penumbra::EncryptionScheme::kCbcs
The `cbcs` scheme: AES-CBC encryption, optionally with a pattern of encrypted and clear blocks.

\struct penumbra::SubsampleEntry
One span of a subsample-encrypted sample: clear bytes followed by encrypted bytes.

\var uint32_t penumbra::SubsampleEntry::clear_bytes
Number of clear bytes at the start of the span.

\var uint32_t penumbra::SubsampleEntry::encrypted_bytes
Number of encrypted bytes after the clear bytes.

\fn constexpr bool penumbra::SubsampleEntry::operator==(const SubsampleEntry&, const SubsampleEntry&)
Compares both byte counts.

\struct penumbra::EncryptionPattern
Pattern of the `cbcs` scheme: encrypted 16-byte blocks followed by clear 16-byte blocks, repeated.

\var uint8_t penumbra::EncryptionPattern::crypt_byte_block
Number of encrypted blocks in each repetition, at most 15.

\var uint8_t penumbra::EncryptionPattern::skip_byte_block
Number of clear blocks in each repetition, at most 15.

\fn constexpr bool penumbra::EncryptionPattern::operator==(const EncryptionPattern&, const EncryptionPattern&)
Compares both block counts.

\struct penumbra::EncryptionInfo
Encryption metadata of one sample.

Check it against the sample with ValidateEncryption().

\var EncryptionScheme penumbra::EncryptionInfo::scheme
Protection scheme; `cenc` by default.

\var std::array<uint8_t, 16> penumbra::EncryptionInfo::key_id
Identifier of the content key.

\var std::vector<uint8_t> penumbra::EncryptionInfo::iv
Initialisation vector: 8 or 16 bytes for `cenc`, 16 bytes for `cbcs`.

\var std::vector<SubsampleEntry> penumbra::EncryptionInfo::subsamples
Subsample map, whose spans must add up to the sample size; empty means that the whole sample is encrypted.

\var std::optional<EncryptionPattern> penumbra::EncryptionInfo::pattern
Encryption pattern, allowed only with the `cbcs` scheme.

\fn bool penumbra::EncryptionInfo::operator==(const EncryptionInfo&, const EncryptionInfo&)
Compares every field.

\enum penumbra::EncryptionCheck
Result of ValidateEncryption().

\var penumbra::EncryptionCheck::kValid
The metadata is consistent with the scheme and the sample.

\var penumbra::EncryptionCheck::kInvalidIvSize
The IV is not 8 or 16 bytes for `cenc`, or not 16 bytes for `cbcs`.

\var penumbra::EncryptionCheck::kPatternWithoutCbcs
A pattern is present but the scheme is not `cbcs`.

\var penumbra::EncryptionCheck::kInvalidPattern
A pattern block count is greater than 15.

\var penumbra::EncryptionCheck::kSubsampleSizeMismatch
The subsample spans do not add up to the sample size.

\fn constexpr std::string_view penumbra::EncryptionCheckName(EncryptionCheck check)
Returns a short description of `check`, such as `invalid IV size`, or `unknown` for an unlisted value.

\fn template <typename Sink> void penumbra::AbslStringify(Sink& sink, EncryptionCheck check)
Lets Abseil formatting and logging print `check` as EncryptionCheckName() does.

\fn EncryptionCheck penumbra::ValidateEncryption(const EncryptionInfo& info, size_t sample_size)
Checks `info` for a sample of `sample_size` bytes.

Checks, in this order, the IV size for the scheme, that a pattern appears only with `cbcs` and fits its 4-bit
fields, and that a non-empty subsample map adds up to exactly `sample_size`. Returns the first failure, or
EncryptionCheck::kValid.
