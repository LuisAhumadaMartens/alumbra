#include "penumbra/interface/encoded_sample.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "penumbra/interface/encryption_info.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "penumbra/interface/stream_info.h"

namespace penumbra {
namespace {

constexpr Rational kTimeBase = Rational::Of(1, 90'000);

class FakePort {
 public:
  void WriteSamples(std::span<const EncodedSample> samples) {
    received_.insert(received_.end(), samples.begin(), samples.end());
  }

  [[nodiscard]] const std::vector<EncodedSample>& received() const {
    return received_;
  }

 private:
  std::vector<EncodedSample> received_;
};

class FakeDemuxer {
 public:
  explicit FakeDemuxer(SharedBytes segment) : segment_(std::move(segment)) {}

  [[nodiscard]] std::vector<EncodedSample> Demux(
      std::span<const size_t> sizes) const {
    std::vector<EncodedSample> samples;
    size_t offset = 0;
    int64_t pts = 0;
    for (size_t const size : sizes) {
      EncodedSample sample;
      sample.track_id = 1;
      sample.data = segment_.Slice(offset, size).value();
      sample.pts = Timestamp{.value = pts, .time_base = kTimeBase};
      sample.dts = Timestamp{.value = pts, .time_base = kTimeBase};
      sample.duration = Timestamp{.value = 3'003, .time_base = kTimeBase};
      sample.key_frame = offset == 0;
      samples.push_back(std::move(sample));
      offset += size;
      pts += 3'003;
    }
    return samples;
  }

 private:
  SharedBytes segment_;
};

TEST(EncodedSampleTest, ReachesThePortWithoutCopyingData) {
  std::vector<uint8_t> bytes(1'000);
  for (size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<uint8_t>(i);
  }
  const SharedBytes segment = SharedBytes::Adopt(std::move(bytes));
  const FakeDemuxer demuxer(segment);
  const std::array<size_t, 3> sizes = {400, 350, 250};

  FakePort port;
  port.WriteSamples(demuxer.Demux(sizes));

  ASSERT_EQ(port.received().size(), 3U);
  EXPECT_EQ(port.received()[0].data.data(), segment.data());
  EXPECT_EQ(port.received()[1].data.data(), segment.span().subspan(400).data());
  EXPECT_EQ(port.received()[2].data.data(), segment.span().subspan(750).data());
  EXPECT_EQ(port.received()[2].data.size(), 250U);
  EXPECT_EQ(port.received()[1].data.span()[0], static_cast<uint8_t>(400));
  EXPECT_TRUE(port.received()[0].key_frame);
  EXPECT_EQ(ToMediaTime(port.received()[1].pts, Rounding::kNearest),
            MediaTime::FromMicroseconds(33'367));
}

TEST(EncodedSampleTest, SharesConfigurationChanges) {
  StreamInfo info;
  info.track_id = 1;
  info.codec = CodecId::kH264;
  info.codec_string = "avc1.64001f";
  info.time_base = kTimeBase;
  auto config = std::make_shared<const StreamInfo>(std::move(info));
  EncodedSample sample;
  sample.config_change = config;
  const EncodedSample copy = sample;
  EXPECT_EQ(copy.config_change.get(), config.get());
}

TEST(EncodedSampleTest, ValidatesEncryptionAgainstItsData) {
  EncodedSample sample;
  sample.data = SharedBytes::Adopt(std::vector<uint8_t>(100));
  EXPECT_EQ(ValidateEncryption(sample), EncryptionCheck::kValid);
  EncryptionInfo info;
  info.iv.assign(8, 0);
  info.subsamples = {{20, 80}};
  sample.encryption = info;
  EXPECT_EQ(ValidateEncryption(sample), EncryptionCheck::kValid);
  sample.encryption->subsamples = {{20, 70}};
  EXPECT_EQ(ValidateEncryption(sample),
            EncryptionCheck::kSubsampleSizeMismatch);
}

TEST(EncodedSampleTest, DefaultsToUnknownTimestamps) {
  const EncodedSample sample;
  EXPECT_FALSE(sample.pts.is_known());
  EXPECT_FALSE(sample.dts.is_known());
  EXPECT_FALSE(sample.duration.is_known());
  EXPECT_FALSE(sample.trim_start.is_known());
  EXPECT_FALSE(sample.encryption.has_value());
}

TEST(EncodedSampleTest, Stringifies) {
  EncodedSample sample;
  sample.track_id = 2;
  sample.data = SharedBytes::Adopt(std::vector<uint8_t>(16));
  sample.pts = Timestamp{.value = 3'003, .time_base = kTimeBase};
  sample.dts = Timestamp{.value = 0, .time_base = kTimeBase};
  sample.duration = Timestamp{.value = 3'003, .time_base = kTimeBase};
  sample.key_frame = true;
  EXPECT_EQ(absl::StrCat(sample),
            "track=2 pts=3003@1/90000 dts=0@1/90000 duration=3003@1/90000 "
            "size=16 key");
}

}
}
