#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "gpu_test_environment.h"
#include "penumbra/desktop/video/color_conversion.h"
#include "penumbra/desktop/video/gpu_presenter.h"
#include "penumbra/desktop/video/presenter.h"
#include "penumbra/desktop/video/video_geometry.h"
#include "penumbra/desktop/video/video_sink.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "test_frames.h"

namespace penumbra::desktop {
namespace {

using testing::GpuTest;
using testing::MakeColourBarFrame;
using testing::TestFrame;

constexpr int kColourBarTolerance = 4;
constexpr int kGoldenMaxTolerance = 4;
constexpr double kGoldenMeanTolerance = 1.0;

struct Pixel {
  int red = 0;
  int green = 0;
  int blue = 0;
};

Pixel PixelAt(const RgbaImage& image, int32_t x, int32_t y) {
  const size_t offset =
      ((static_cast<size_t>(y) * static_cast<size_t>(image.size.width)) +
       static_cast<size_t>(x)) *
      4;
  return Pixel{.red = image.pixels.at(offset),
               .green = image.pixels.at(offset + 1),
               .blue = image.pixels.at(offset + 2)};
}

double DecodeComponent(uint16_t code, int bit_depth, bool full_range,
                       bool chroma) {
  const auto scale = static_cast<double>(1 << (bit_depth - 8));
  if (full_range) {
    const auto span = static_cast<double>((1 << bit_depth) - 1);
    return chroma ? (code - (128.0 * scale)) / span : code / span;
  }
  if (chroma) {
    return (code - (128.0 * scale)) / (224.0 * scale);
  }
  return (code - (16.0 * scale)) / (219.0 * scale);
}

double Interpolate(const TestFrame& frame, double chroma_x, double chroma_y,
                   size_t component) {
  const int32_t last_x = frame.Layout().chroma_width - 1;
  const int32_t last_y = frame.Layout().chroma_height - 1;
  const double x = std::clamp(chroma_x, 0.0, static_cast<double>(last_x));
  const double y = std::clamp(chroma_y, 0.0, static_cast<double>(last_y));
  const auto x0 = static_cast<int32_t>(std::floor(x));
  const auto y0 = static_cast<int32_t>(std::floor(y));
  const int32_t x1 = std::min(x0 + 1, last_x);
  const int32_t y1 = std::min(y0 + 1, last_y);
  const double fx = x - x0;
  const double fy = y - y0;
  const auto code = [&](int32_t cx, int32_t cy) {
    return static_cast<double>(frame.ChromaCodes(cx, cy).at(component));
  };
  return (((code(x0, y0) * (1 - fx)) + (code(x1, y0) * fx)) * (1 - fy)) +
         (((code(x0, y1) * (1 - fx)) + (code(x1, y1) * fx)) * fy);
}

RgbaImage ReferenceRender(const TestFrame& frame) {
  const DecodedVideoFrame& decoded = frame.Frame();
  const ColorSpace& color = decoded.color;
  const int bit_depth = frame.Layout().bit_depth;
  const MatrixStandard standard =
      ResolveMatrixStandard(color.matrix, decoded.width, decoded.height);
  const LumaWeights weights = WeightsOf(standard);
  const double green_weight = 1.0 - weights.red - weights.blue;
  const ChromaSiting siting = ResolveChromaSiting(color.chroma_siting);
  const double offset_x = siting == ChromaSiting::kCenter ? 0.5 : 0.0;
  const double offset_y = siting == ChromaSiting::kTopLeft ? 0.0 : 0.5;
  RgbaImage image;
  image.size = testing::Size(decoded.width, decoded.height);
  image.pixels.resize(static_cast<size_t>(decoded.width) *
                      static_cast<size_t>(decoded.height) * 4);
  for (int32_t y = 0; y < decoded.height; ++y) {
    for (int32_t x = 0; x < decoded.width; ++x) {
      const double luma = DecodeComponent(frame.LumaCode(x, y), bit_depth,
                                          color.full_range, false);
      const double chroma_x = (x - offset_x) / 2.0;
      const double chroma_y = (y - offset_y) / 2.0;
      const double blue = DecodeComponent(
          static_cast<uint16_t>(
              std::lround(Interpolate(frame, chroma_x, chroma_y, 0))),
          bit_depth, color.full_range, true);
      const double red = DecodeComponent(
          static_cast<uint16_t>(
              std::lround(Interpolate(frame, chroma_x, chroma_y, 1))),
          bit_depth, color.full_range, true);
      const double r = luma + (2.0 * (1.0 - weights.red) * red);
      const double b = luma + (2.0 * (1.0 - weights.blue) * blue);
      const double g =
          (luma - (weights.red * r) - (weights.blue * b)) / green_weight;
      const std::array<double, 3> rgb = {r, g, b};
      const size_t offset =
          ((static_cast<size_t>(y) * static_cast<size_t>(decoded.width)) +
           static_cast<size_t>(x)) *
          4;
      for (size_t channel = 0; channel < 3; ++channel) {
        image.pixels.at(offset + channel) = static_cast<uint8_t>(
            std::lround(std::clamp(rgb.at(channel), 0.0, 1.0) * 255.0));
      }
      image.pixels.at(offset + 3) = 255;
    }
  }
  return image;
}

TestFrame MakeDetailFrame(PixelFormat format, const ColorSpace& color) {
  constexpr int32_t kWidth = 64;
  constexpr int32_t kHeight = 48;
  TestFrame frame(format, kWidth, kHeight, color);
  const int bit_depth = frame.Layout().bit_depth;
  const int scale = 1 << (bit_depth - 8);
  uint32_t state = 12345;
  const auto next = [&state]() {
    state = (state * 1664525U) + 1013904223U;
    return state >> 24;
  };
  for (int32_t y = 0; y < kHeight; ++y) {
    for (int32_t x = 0; x < kWidth; ++x) {
      const bool checker = ((x / 4) + (y / 4)) % 2 == 0;
      const int base = color.full_range ? 0 : 16;
      const int span = color.full_range ? 255 : 219;
      const int ramp = base + ((span * x) / (kWidth - 1));
      const int code = checker ? ramp : base + (span - (ramp - base));
      frame.SetLuma(x, y, static_cast<uint16_t>(code * scale));
    }
  }
  for (int32_t y = 0; y < frame.Layout().chroma_height; ++y) {
    for (int32_t x = 0; x < frame.Layout().chroma_width; ++x) {
      const int blue = 64 + static_cast<int>(next() % 128U);
      const int red = 64 + static_cast<int>(next() % 128U);
      frame.SetChroma(x, y, static_cast<uint16_t>(blue * scale),
                      static_cast<uint16_t>(red * scale));
    }
  }
  return frame;
}

void ExpectWithin(const RgbaImage& actual, const RgbaImage& expected,
                  int max_tolerance, double mean_tolerance) {
  ASSERT_EQ(actual.size, expected.size);
  int worst = 0;
  double total = 0;
  size_t samples = 0;
  for (size_t index = 0; index < actual.pixels.size(); ++index) {
    if (index % 4 == 3) {
      continue;
    }
    const int difference =
        std::abs(static_cast<int>(actual.pixels.at(index)) -
                 static_cast<int>(expected.pixels.at(index)));
    worst = std::max(worst, difference);
    total += difference;
    ++samples;
  }
  EXPECT_LE(worst, max_tolerance);
  EXPECT_LE(total / static_cast<double>(samples), mean_tolerance);
}

class VideoGoldenTest : public GpuTest {
 protected:
  static std::unique_ptr<GpuPresenter> CreateSized(PixelSize size) {
    GpuPresenterOptions options;
    options.offscreen_logical_size = size;
    return CreatePresenter(options);
  }

  static RgbaImage Render(GpuPresenter& presenter, const TestFrame& frame,
                          Rational sar = Rational::Of(1, 1)) {
    PresentRequest request;
    request.frame = &frame.Frame();
    request.sample_aspect_ratio = sar;
    EXPECT_TRUE(presenter.Present(request).ok());
    absl::StatusOr<RgbaImage> image = presenter.ReadbackTarget();
    EXPECT_TRUE(image.ok());
    return image.ok() ? *std::move(image) : RgbaImage{};
  }
};

struct BarCase {
  PixelFormat format;
  MatrixStandard standard;
  uint8_t matrix_code;
  bool full_range;
};

class ColourBarTest : public VideoGoldenTest,
                      public ::testing::WithParamInterface<BarCase> {};

TEST_P(ColourBarTest, RendersReferenceRgbWithinTolerance) {
  if (!available_) {
    return;
  }
  const BarCase& test = GetParam();
  auto presenter = CreateSized(testing::Size(128, 32));
  if (presenter == nullptr) {
    return;
  }
  ColorSpace color;
  color.matrix = test.matrix_code;
  color.full_range = test.full_range;
  const TestFrame frame =
      MakeColourBarFrame(test.format, 128, 32, color, test.standard);
  const RgbaImage image = Render(*presenter, frame);
  ASSERT_EQ(image.size, (testing::Size(128, 32)));
  for (size_t bar = 0; bar < testing::ColourBars().size(); ++bar) {
    const testing::Rgb& expected = testing::ColourBars().at(bar);
    const Pixel actual =
        PixelAt(image, (static_cast<int32_t>(bar) * 16) + 8, 16);
    EXPECT_NEAR(actual.red, expected.red * 255.0, kColourBarTolerance) << bar;
    EXPECT_NEAR(actual.green, expected.green * 255.0, kColourBarTolerance)
        << bar;
    EXPECT_NEAR(actual.blue, expected.blue * 255.0, kColourBarTolerance) << bar;
  }
}

INSTANTIATE_TEST_SUITE_P(
    Bt601AndBt709LimitedAndFull, ColourBarTest,
    ::testing::Values(
        BarCase{PixelFormat::kI420, MatrixStandard::kBt601, 6, false},
        BarCase{PixelFormat::kI420, MatrixStandard::kBt601, 6, true},
        BarCase{PixelFormat::kI420, MatrixStandard::kBt709, 1, false},
        BarCase{PixelFormat::kI420, MatrixStandard::kBt709, 1, true},
        BarCase{PixelFormat::kNv12, MatrixStandard::kBt601, 6, false},
        BarCase{PixelFormat::kNv12, MatrixStandard::kBt709, 1, true},
        BarCase{PixelFormat::kP010, MatrixStandard::kBt601, 6, false},
        BarCase{PixelFormat::kP010, MatrixStandard::kBt709, 1, false},
        BarCase{PixelFormat::kP010, MatrixStandard::kBt709, 1, true}));

TEST_F(VideoGoldenTest, WrongMatrixTagIsDetectable) {
  if (!available_) {
    return;
  }
  auto presenter = CreateSized(testing::Size(128, 32));
  if (presenter == nullptr) {
    return;
  }
  ColorSpace tagged;
  tagged.matrix = 1;
  const TestFrame frame = MakeColourBarFrame(PixelFormat::kI420, 128, 32,
                                             tagged, MatrixStandard::kBt601);
  const RgbaImage image = Render(*presenter, frame);
  const Pixel red = PixelAt(image, (5 * 16) + 8, 16);
  EXPECT_GT(std::abs(red.green - 0), kColourBarTolerance);
}

struct GoldenCase {
  PixelFormat format;
  uint8_t matrix_code;
  bool full_range;
  ChromaSiting siting;
};

class GoldenFrameTest : public VideoGoldenTest,
                        public ::testing::WithParamInterface<GoldenCase> {};

TEST_P(GoldenFrameTest, MatchesCpuReferenceFrame) {
  if (!available_) {
    return;
  }
  const GoldenCase& test = GetParam();
  auto presenter = CreateSized(testing::Size(64, 48));
  if (presenter == nullptr) {
    return;
  }
  ColorSpace color;
  color.matrix = test.matrix_code;
  color.full_range = test.full_range;
  color.chroma_siting = test.siting;
  const TestFrame frame = MakeDetailFrame(test.format, color);
  const RgbaImage actual = Render(*presenter, frame);
  const RgbaImage expected = ReferenceRender(frame);
  ExpectWithin(actual, expected, kGoldenMaxTolerance, kGoldenMeanTolerance);
}

INSTANTIATE_TEST_SUITE_P(
    FormatsRangesAndSiting, GoldenFrameTest,
    ::testing::Values(
        GoldenCase{PixelFormat::kI420, 1, false, ChromaSiting::kLeft},
        GoldenCase{PixelFormat::kI420, 1, false, ChromaSiting::kCenter},
        GoldenCase{PixelFormat::kI420, 6, true, ChromaSiting::kTopLeft},
        GoldenCase{PixelFormat::kNv12, 1, false, ChromaSiting::kUnspecified},
        GoldenCase{PixelFormat::kNv12, 6, true, ChromaSiting::kCenter},
        GoldenCase{PixelFormat::kP010, 1, false, ChromaSiting::kLeft},
        GoldenCase{PixelFormat::kP010, 9, true, ChromaSiting::kTopLeft}));

TEST_F(VideoGoldenTest, ChromaSitingChangesTheRenderedFrame) {
  if (!available_) {
    return;
  }
  auto presenter = CreateSized(testing::Size(64, 48));
  if (presenter == nullptr) {
    return;
  }
  ColorSpace left;
  left.matrix = 1;
  left.chroma_siting = ChromaSiting::kLeft;
  ColorSpace center = left;
  center.chroma_siting = ChromaSiting::kCenter;
  const TestFrame left_frame = MakeDetailFrame(PixelFormat::kI420, left);
  const TestFrame center_frame = MakeDetailFrame(PixelFormat::kI420, center);
  const RgbaImage left_image = Render(*presenter, left_frame);
  const RgbaImage center_image = Render(*presenter, center_frame);
  const RgbaImage center_reference = ReferenceRender(center_frame);
  int differing = 0;
  for (size_t index = 0; index < left_image.pixels.size(); ++index) {
    if (std::abs(static_cast<int>(left_image.pixels[index]) -
                 static_cast<int>(center_image.pixels[index])) > 8) {
      ++differing;
    }
  }
  EXPECT_GT(differing, 100);
  ExpectWithin(center_image, center_reference, kGoldenMaxTolerance,
               kGoldenMeanTolerance);
}

TEST_F(VideoGoldenTest, AnamorphicClipUsesDisplayAspectRatio) {
  if (!available_) {
    return;
  }
  auto presenter = CreateSized(testing::Size(800, 480));
  if (presenter == nullptr) {
    return;
  }
  ColorSpace color;
  color.matrix = 6;
  const TestFrame frame = MakeColourBarFrame(PixelFormat::kI420, 720, 480,
                                             color, MatrixStandard::kBt601);
  const RgbaImage anamorphic = Render(*presenter, frame, Rational::Of(8, 9));
  EXPECT_EQ(PixelAt(anamorphic, 80 - 3, 240).red, 0);
  EXPECT_EQ(PixelAt(anamorphic, 80 - 3, 240).blue, 0);
  EXPECT_GT(PixelAt(anamorphic, 80 + 3, 240).green, 200);
  EXPECT_GT(PixelAt(anamorphic, 240 - 4, 240).red, 200);
  EXPECT_LT(PixelAt(anamorphic, 240 + 4, 240).red, 50);

  const RgbaImage square = Render(*presenter, frame, Rational::Of(1, 1));
  EXPECT_EQ(PixelAt(square, 40 - 3, 240).green, 0);
  EXPECT_GT(PixelAt(square, 40 + 3, 240).green, 200);
  EXPECT_GT(PixelAt(square, 220 - 4, 240).red, 200);
  EXPECT_LT(PixelAt(square, 220 + 4, 240).red, 50);
}

TEST_F(VideoGoldenTest, WideFrameIsLetterboxed) {
  if (!available_) {
    return;
  }
  auto presenter = CreateSized(testing::Size(320, 320));
  if (presenter == nullptr) {
    return;
  }
  ColorSpace color;
  color.matrix = 1;
  const TestFrame frame = MakeColourBarFrame(PixelFormat::kNv12, 160, 90, color,
                                             MatrixStandard::kBt709);
  const RgbaImage image = Render(*presenter, frame);
  EXPECT_EQ(PixelAt(image, 100, 60).green, 0);
  EXPECT_GT(PixelAt(image, 20, 160).green, 200);
  EXPECT_EQ(PixelAt(image, 100, 260).green, 0);
  EXPECT_GT(PixelAt(image, 100, 72).green, 200);
  EXPECT_GT(PixelAt(image, 100, 248).green, 200);
}

TEST_F(VideoGoldenTest, BoundsRestrictTheVideoRectangle) {
  if (!available_) {
    return;
  }
  auto presenter = CreateSized(testing::Size(200, 200));
  if (presenter == nullptr) {
    return;
  }
  ColorSpace color;
  color.matrix = 1;
  const TestFrame frame = MakeColourBarFrame(PixelFormat::kI420, 64, 64, color,
                                             MatrixStandard::kBt709);
  PresentRequest request;
  request.frame = &frame.Frame();
  request.bounds = testing::Rect(100, 100, 100, 100);
  ASSERT_TRUE(presenter->Present(request).ok());
  const RgbaImage image = *presenter->ReadbackTarget();
  EXPECT_EQ(PixelAt(image, 50, 50).green, 0);
  EXPECT_GT(PixelAt(image, 106, 150).green, 200);
}

TEST_F(VideoGoldenTest, ScalingFilterSelectsInterpolation) {
  if (!available_) {
    return;
  }
  GpuPresenterOptions options;
  options.offscreen_logical_size = testing::Size(16, 16);
  options.scaling_filter = ScalingFilter::kNearest;
  auto presenter = CreatePresenter(options);
  if (presenter == nullptr) {
    return;
  }
  ColorSpace color;
  color.full_range = true;
  color.matrix = 1;
  TestFrame frame(PixelFormat::kI420, 4, 4, color);
  for (int32_t y = 0; y < 4; ++y) {
    for (int32_t x = 0; x < 4; ++x) {
      frame.SetLuma(x, y, (x + y) % 2 == 0 ? 255 : 0);
    }
  }
  for (int32_t y = 0; y < 2; ++y) {
    for (int32_t x = 0; x < 2; ++x) {
      frame.SetChroma(x, y, 128, 128);
    }
  }
  const RgbaImage nearest = Render(*presenter, frame);
  EXPECT_EQ(PixelAt(nearest, 0, 0).red, PixelAt(nearest, 3, 0).red);
  EXPECT_EQ(PixelAt(nearest, 4, 0).red, PixelAt(nearest, 7, 0).red);
  EXPECT_NE(PixelAt(nearest, 3, 0).red, PixelAt(nearest, 4, 0).red);

  ASSERT_TRUE(presenter->SetScalingFilter(ScalingFilter::kLinear).ok());
  const RgbaImage linear = Render(*presenter, frame);
  const int middle = PixelAt(linear, 5, 0).red;
  EXPECT_GT(middle, 5);
  EXPECT_LT(middle, 250);
}

TEST_F(VideoGoldenTest, OpaqueSurfaceMatchesCpuPlanes) {
  if (!available_) {
    return;
  }
  auto presenter = CreateSized(testing::Size(64, 48));
  if (presenter == nullptr) {
    return;
  }
  ColorSpace color;
  color.matrix = 1;
  const TestFrame cpu = MakeDetailFrame(PixelFormat::kNv12, color);
  const RgbaImage from_planes = Render(*presenter, cpu);

  absl::StatusOr<std::shared_ptr<const GpuSurfaceHandle>> surface =
      presenter->CreateTestSurface(cpu.Frame());
  ASSERT_TRUE(surface.ok()) << surface.status();
  DecodedVideoFrame opaque;
  opaque.format = PixelFormat::kOpaque;
  opaque.width = cpu.Frame().width;
  opaque.height = cpu.Frame().height;
  opaque.color = color;
  opaque.storage = MakeOpaqueStorage(*std::move(surface));
  PresentRequest request;
  request.frame = &opaque;
  ASSERT_TRUE(presenter->Present(request).ok());
  const RgbaImage from_surface = *presenter->ReadbackTarget();
  ExpectWithin(from_surface, from_planes, 1, 0.1);
  EXPECT_EQ(presenter->Capabilities().imported_surface_kinds.front(),
            SurfaceKind::kTexturePlanes);
}

TEST_F(VideoGoldenTest, OpaqueFrameWithoutSurfaceIsDropped) {
  if (!available_) {
    return;
  }
  auto presenter = CreateSized(testing::Size(32, 32));
  if (presenter == nullptr) {
    return;
  }
  DecodedVideoFrame opaque;
  opaque.format = PixelFormat::kOpaque;
  opaque.width = 16;
  opaque.height = 16;
  PresentRequest request;
  request.frame = &opaque;
  EXPECT_FALSE(presenter->Present(request).ok());
  EXPECT_EQ(presenter->Stats().dropped_frames, 1U);
  EXPECT_EQ(presenter->Stats().presented_frames, 0U);
}

class RecordingObserver final : public PresenterObserver {
 public:
  void OnRefreshRateChanged(const RefreshRate& rate) override {
    last_rate = rate;
  }
  void OnPresented(const PresentRecord& record) override {
    records.push_back(record);
  }

  RefreshRate last_rate;
  std::vector<PresentRecord> records;
};

TEST_F(VideoGoldenTest, CountsPresentedAndDroppedFramesAndReportsTimestamps) {
  if (!available_) {
    return;
  }
  auto presenter = CreateSized(testing::Size(64, 64));
  if (presenter == nullptr) {
    return;
  }
  auto observer = std::make_shared<RecordingObserver>();
  presenter->SetObserver(observer);
  ColorSpace color;
  color.matrix = 1;
  TestFrame frame = MakeColourBarFrame(PixelFormat::kI420, 64, 64, color,
                                       MatrixStandard::kBt709);
  for (int index = 0; index < 5; ++index) {
    frame.Frame().presentation_time =
        MediaTime::FromMilliseconds(static_cast<int64_t>(index) * 40);
    PresentRequest request;
    request.frame = &frame.Frame();
    ASSERT_TRUE(presenter->Present(request).ok());
  }
  for (int index = 0; index < 3; ++index) {
    ASSERT_TRUE(presenter->Present(PresentRequest{}).ok());
  }
  TestFrame broken(PixelFormat::kI420, 64, 64, color);
  broken.Frame().planes[0].data = nullptr;
  PresentRequest bad;
  bad.frame = &broken.Frame();
  EXPECT_FALSE(presenter->Present(bad).ok());

  const PresenterStats stats = presenter->Stats();
  EXPECT_EQ(stats.presented_frames, 5U);
  EXPECT_EQ(stats.repeated_refreshes, 3U);
  EXPECT_EQ(stats.dropped_frames, 1U);
  EXPECT_EQ(stats.last_presented_media_time, MediaTime::FromMilliseconds(160));
  ASSERT_EQ(observer->records.size(), 8U);
  EXPECT_TRUE(observer->records.front().new_frame);
  EXPECT_FALSE(observer->records.back().new_frame);
  EXPECT_LE(observer->records[0].submit_ticks_ns,
            observer->records[1].submit_ticks_ns);
  EXPECT_LE(observer->records[0].acquire_ticks_ns,
            observer->records[0].submit_ticks_ns);
  EXPECT_EQ(observer->records[0].video_rect, (testing::Rect(0, 0, 64, 64)));
}

TEST_F(VideoGoldenTest, ReportsCapabilitiesAndUnknownOffscreenRefreshRate) {
  if (!available_) {
    return;
  }
  auto presenter = CreateSized(testing::Size(32, 32));
  if (presenter == nullptr) {
    return;
  }
  const PresenterCapabilities capabilities = presenter->Capabilities();
  EXPECT_TRUE(capabilities.offscreen);
  EXPECT_FALSE(capabilities.backend.empty());
  EXPECT_EQ(capabilities.present_timing, PresentTimingSupport::kNone);
  EXPECT_FALSE(capabilities.present_timing_fallback_reason.empty());
  EXPECT_FALSE(presenter->CurrentRefreshRate().is_known());
  EXPECT_TRUE(presenter->CurrentRefreshRate().Interval().is_unknown());
}

TEST_F(VideoGoldenTest, RejectsInvalidFramesInFlight) {
  if (!available_) {
    return;
  }
  GpuPresenterOptions options;
  options.frames_in_flight = 4;
  EXPECT_FALSE(GpuPresenter::Create(options).ok());
}

class ScriptedFrames final : public VideoFrameSource {
 public:
  explicit ScriptedFrames(std::vector<DecodedVideoFrame> frames)
      : frames_(std::move(frames)) {}

  std::optional<DecodedVideoFrame> TakeDueFrame(
      MediaTime time) noexcept override {
    static_cast<void>(time);
    ++request_count;
    if (next_ >= frames_.size() || (next_ % 2 == 1)) {
      ++next_;
      return std::nullopt;
    }
    return frames_[next_++];
  }

  size_t request_count = 0;

 private:
  std::vector<DecodedVideoFrame> frames_;
  size_t next_ = 0;
};

TEST_F(VideoGoldenTest, VideoSinkPullsDueFramesAndReportsQuality) {
  if (!available_) {
    return;
  }
  auto presenter = CreateSized(testing::Size(64, 64));
  if (presenter == nullptr) {
    return;
  }
  ColorSpace color;
  color.matrix = 1;
  const TestFrame frame = MakeColourBarFrame(PixelFormat::kI420, 64, 64, color,
                                             MatrixStandard::kBt709);
  auto source = std::make_shared<ScriptedFrames>(
      std::vector<DecodedVideoFrame>(4, frame.Frame()));
  DesktopVideoSink sink(std::move(presenter));
  EXPECT_FALSE(sink.RunFrame(MediaTime::Zero()).ok());
  ASSERT_TRUE(sink.Start(source).ok());
  VideoInfo info;
  info.sample_aspect_ratio = Rational::Of(1, 1);
  sink.SetVideoInfo(info);
  ASSERT_TRUE(sink.SetBounds(OutputBounds{0, 0, 0, 0, 0}).ok());
  for (int index = 0; index < 4; ++index) {
    ASSERT_TRUE(sink.RunFrame(MediaTime::FromMilliseconds(
                                  static_cast<int64_t>(index) * 16))
                    .ok());
  }
  EXPECT_EQ(source->request_count, 4U);
  EXPECT_EQ(sink.PlaybackQuality().presented_frames, 2U);
  EXPECT_EQ(sink.PlaybackQuality().dropped_frames, 0U);
  ASSERT_TRUE(sink.Stop().ok());
  EXPECT_FALSE(sink.RunFrame(MediaTime::Zero()).ok());
}

TEST_F(VideoGoldenTest, RepeatedResizeAndPixelDensityChangesLeakNothing) {
  if (!available_) {
    return;
  }
  auto presenter = CreateSized(testing::Size(64, 64));
  if (presenter == nullptr) {
    return;
  }
  const std::shared_ptr<GpuContext> context = presenter->Context();
  ColorSpace color;
  color.matrix = 1;
  const TestFrame frame = MakeColourBarFrame(PixelFormat::kI420, 64, 48, color,
                                             MatrixStandard::kBt709);
  PresentRequest request;
  request.frame = &frame.Frame();
  ASSERT_TRUE(presenter->Present(request).ok());
  const int64_t baseline = context->counters.Live();
  const std::array<float, 4> densities = {1.0F, 1.5F, 2.0F, 3.0F};
  for (int index = 0; index < 200; ++index) {
    const PixelSize logical =
        testing::Size(32 + ((index * 7) % 160), 24 + ((index * 5) % 120));
    const float density =
        densities.at(static_cast<size_t>(index) % densities.size());
    ASSERT_TRUE(presenter->ResizeOffscreen(logical, density).ok());
    EXPECT_EQ(presenter->TargetPixels(), ScaleToPixels(logical, density));
    ASSERT_TRUE(presenter->Present(request).ok());
    if (index % 25 == 0) {
      const absl::StatusOr<RgbaImage> image = presenter->ReadbackTarget();
      ASSERT_TRUE(image.ok());
      EXPECT_EQ(image->size, ScaleToPixels(logical, density));
    }
    EXPECT_EQ(context->counters.Live(), baseline);
  }
  presenter.reset();
  EXPECT_EQ(context->counters.Live(), 0);
  EXPECT_EQ(context->counters.textures.load(), 0);
  EXPECT_EQ(context->counters.transfer_buffers.load(), 0);
}

TEST_F(VideoGoldenTest, ChangingFrameSizeAndFormatReplacesPlaneTextures) {
  if (!available_) {
    return;
  }
  auto presenter = CreateSized(testing::Size(64, 64));
  if (presenter == nullptr) {
    return;
  }
  const std::shared_ptr<GpuContext> context = presenter->Context();
  ColorSpace color;
  color.matrix = 1;
  const std::array<std::pair<PixelFormat, int32_t>, 6> sequence = {{
      {PixelFormat::kI420, 64},
      {PixelFormat::kNv12, 48},
      {PixelFormat::kP010, 32},
      {PixelFormat::kI420, 64},
      {PixelFormat::kNv12, 48},
      {PixelFormat::kP010, 32},
  }};
  std::vector<int64_t> counts;
  for (const auto& [format, width] : sequence) {
    const TestFrame frame =
        MakeColourBarFrame(format, width, 16, color, MatrixStandard::kBt709);
    PresentRequest request;
    request.frame = &frame.Frame();
    ASSERT_TRUE(presenter->Present(request).ok());
    counts.push_back(context->counters.Live());
  }
  EXPECT_EQ(counts[0], counts[3]);
  EXPECT_EQ(counts[1], counts[4]);
  EXPECT_EQ(counts[2], counts[5]);
  presenter.reset();
  EXPECT_EQ(context->counters.Live(), 0);
}

}
}
