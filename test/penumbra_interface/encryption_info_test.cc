#include "penumbra/interface/encryption_info.h"

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace penumbra {
namespace {

EncryptionInfo Cenc(std::vector<SubsampleEntry> subsamples) {
  EncryptionInfo info;
  info.scheme = EncryptionScheme::kCenc;
  info.key_id.fill(0xAB);
  info.iv.assign(8, 0x01);
  info.subsamples = std::move(subsamples);
  return info;
}

EncryptionInfo Cbcs() {
  EncryptionInfo info;
  info.scheme = EncryptionScheme::kCbcs;
  info.iv.assign(16, 0x02);
  info.pattern = EncryptionPattern{.crypt_byte_block=1, .skip_byte_block=9};
  info.subsamples = {{100, 400}};
  return info;
}

TEST(EncryptionInfoTest, AcceptsSubsamplesThatCoverTheSample) {
  EXPECT_EQ(ValidateEncryption(Cenc({{10, 90}, {5, 0}}), 105),
            EncryptionCheck::kValid);
  EXPECT_EQ(ValidateEncryption(Cbcs(), 500), EncryptionCheck::kValid);
}

TEST(EncryptionInfoTest, TreatsMissingSubsamplesAsFullSampleEncryption) {
  EXPECT_EQ(ValidateEncryption(Cenc({}), 4'096), EncryptionCheck::kValid);
}

TEST(EncryptionInfoTest, RejectsSubsampleMapsThatDoNotSumToTheSampleSize) {
  EXPECT_EQ(ValidateEncryption(Cenc({{10, 90}}), 101),
            EncryptionCheck::kSubsampleSizeMismatch);
  EXPECT_EQ(ValidateEncryption(Cenc({{10, 90}}), 99),
            EncryptionCheck::kSubsampleSizeMismatch);
  EXPECT_EQ(ValidateEncryption(Cenc({{0, 0}}), 1),
            EncryptionCheck::kSubsampleSizeMismatch);
}

TEST(EncryptionInfoTest, SumsLargeSubsampleMapsWithoutWrapping) {
  constexpr uint32_t kMax = std::numeric_limits<uint32_t>::max();
  EXPECT_EQ(ValidateEncryption(Cenc({{kMax, kMax}, {kMax, kMax}}), 2),
            EncryptionCheck::kSubsampleSizeMismatch);
  EXPECT_EQ(ValidateEncryption(Cenc({{kMax, 1}}), uint64_t{kMax} + 1),
            EncryptionCheck::kValid);
}

TEST(EncryptionInfoTest, ChecksIvSizePerScheme) {
  EncryptionInfo cenc = Cenc({});
  cenc.iv.assign(16, 0);
  EXPECT_EQ(ValidateEncryption(cenc, 1), EncryptionCheck::kValid);
  cenc.iv.assign(12, 0);
  EXPECT_EQ(ValidateEncryption(cenc, 1), EncryptionCheck::kInvalidIvSize);
  EncryptionInfo cbcs = Cbcs();
  cbcs.iv.assign(8, 0);
  EXPECT_EQ(ValidateEncryption(cbcs, 500), EncryptionCheck::kInvalidIvSize);
}

TEST(EncryptionInfoTest, AllowsPatternsOnlyForCbcs) {
  EncryptionInfo cenc = Cenc({});
  cenc.pattern = EncryptionPattern{.crypt_byte_block=1, .skip_byte_block=9};
  EXPECT_EQ(ValidateEncryption(cenc, 1), EncryptionCheck::kPatternWithoutCbcs);
  EncryptionInfo cbcs = Cbcs();
  cbcs.pattern = EncryptionPattern{.crypt_byte_block=16, .skip_byte_block=0};
  EXPECT_EQ(ValidateEncryption(cbcs, 500), EncryptionCheck::kInvalidPattern);
  cbcs.pattern.reset();
  EXPECT_EQ(ValidateEncryption(cbcs, 500), EncryptionCheck::kValid);
}

TEST(EncryptionInfoTest, StringifiesChecks) {
  EXPECT_EQ(absl::StrCat(EncryptionCheck::kSubsampleSizeMismatch),
            "subsample sizes do not match the sample size");
}

}
}
