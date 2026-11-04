#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "penumbra/desktop/decode_test_util.h"
#include "penumbra/desktop/desktop_component_factory.h"
#include "penumbra/interface/components.h"
#include "penumbra/interface/decoded_frame.h"
#include "penumbra/interface/decoder_types.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"

namespace {

int Fail(std::string_view message) {
  std::cerr << message << '\n';
  return 1;
}

int Run(std::span<char*> arguments) {
  if (arguments.size() != 8U) {
    return Fail(
        "expected stream, codec, MD5, width, height, frames and bit depth");
  }
  std::ifstream file(arguments[1], std::ios::binary);
  if (!file.good()) {
    return Fail("cannot read conformance bitstream");
  }
  const std::vector<uint8_t> bytes = std::vector<uint8_t>(
      std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  const auto streams = penumbra::desktop::decode_test::ReadClip(bytes);
  if (!streams.ok() || streams->size() != 1U) {
    return Fail("conformance bitstream did not produce one video stream");
  }
  const auto parse_integer = [](const char* value) -> std::optional<int> {
    try {
      return std::stoi(value);
    } catch (const std::exception&) {
      return std::nullopt;
    }
  };
  const auto width = parse_integer(arguments[4]);
  const auto height = parse_integer(arguments[5]);
  const auto frames = parse_integer(arguments[6]);
  const auto bit_depth = parse_integer(arguments[7]);
  if (!width || !height || !frames || !bit_depth) {
    return Fail("invalid conformance vector dimensions");
  }
  const penumbra::desktop::decode_test::ClipStream& stream = streams->front();
  if (!std::holds_alternative<penumbra::VideoInfo>(stream.info.format)) {
    return Fail("conformance vector is not video");
  }
  const auto& video_info = std::get<penumbra::VideoInfo>(stream.info.format);
  if (video_info.coded_width != *width || video_info.coded_height != *height) {
    return Fail("conformance bitstream dimensions differ from the manifest");
  }
  penumbra::desktop::DesktopDecoderOptions options;
  options.video_threads = 1;
  penumbra::desktop::DesktopComponentFactory factory(options);
  const std::string decoder_name = std::string("ffmpeg.") + arguments[2];
  const penumbra::DecoderCandidate candidate =
      penumbra::desktop::decode_test::FindCandidate(
          factory, penumbra::StreamType::kVideo, decoder_name);
  const auto callbacks =
      std::make_shared<penumbra::desktop::decode_test::EventCollector>();
  const penumbra::StreamConfig config{
      .info = stream.info,
      .encryption_scheme = std::nullopt,
      .output_mode = penumbra::OutputMode::kTexture};
  auto created = factory.CreateDecoder(candidate, config, 1, callbacks);
  if (!created.ok()) {
    return Fail("cannot create the selected conformance decoder");
  }
  std::unique_ptr<penumbra::Decoder> decoder = std::move(*created);
  const penumbra::desktop::decode_test::DecodeOutput output =
      penumbra::desktop::decode_test::RunDecode({.decoder = decoder.get(),
                                                 .events = callbacks.get(),
                                                 .ticket = 1,
                                                 .samples = stream.samples,
                                                 .end_stream = true,
                                                 .stop_after_writes = 0});
  const absl::Status stop_status = decoder->Stop();
  if (!stop_status.ok() || output.timed_out || !output.errors.empty() ||
      !output.end_of_stream ||
      output.video.size() != static_cast<size_t>(*frames)) {
    return Fail("conformance decoder did not produce the expected frame count");
  }
  const penumbra::PixelFormat expected_format =
      *bit_depth == 8 ? penumbra::PixelFormat::kI420
                      : penumbra::PixelFormat::kI010;
  for (const penumbra::DecodedVideoFrame& frame : output.video) {
    if (frame.format != expected_format) {
      return Fail(
          "decoded pixel format differs from the conformance bit depth");
    }
  }
  if (penumbra::desktop::decode_test::DigestVideoStream(output.video) !=
      arguments[3]) {
    return Fail("decoded output MD5 differs from the published reference");
  }
  return 0;
}

}

int main(int argc, char** argv) {
  try {
    return Run(std::span<char*>(argv, static_cast<size_t>(argc)));
  } catch (const std::exception&) {
    return Fail("failed to process conformance vector arguments");
  }
}
