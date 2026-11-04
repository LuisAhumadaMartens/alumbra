#ifndef TEST_ANTUMBRA_DESKTOP_UI_GOLDEN_IMAGE_H_
#define TEST_ANTUMBRA_DESKTOP_UI_GOLDEN_IMAGE_H_

#include <zlib.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "penumbra/desktop/video/presenter.h"

namespace alumbra::desktop_ui::testing {

struct Comparison {
  int max_difference = 0;
  double mean_difference = 0;
  double exceeding_fraction = 0;
};

inline void AppendBigEndian(std::string* output, std::uint32_t value) {
  output->push_back(static_cast<char>((value >> 24) & 0xff));
  output->push_back(static_cast<char>((value >> 16) & 0xff));
  output->push_back(static_cast<char>((value >> 8) & 0xff));
  output->push_back(static_cast<char>(value & 0xff));
}

inline void AppendChunk(std::string* output, const char* type,
                        std::string_view data) {
  AppendBigEndian(output, static_cast<std::uint32_t>(data.size()));
  const std::size_t start = output->size();
  output->append(type, 4);
  output->append(data);
  const std::string_view payload(*output);
  const uLong checksum =
      crc32(0, reinterpret_cast<const Bytef*>(payload.substr(start).data()),
            static_cast<uInt>(output->size() - start));
  AppendBigEndian(output, static_cast<std::uint32_t>(checksum));
}

inline std::string ToPng(const penumbra::desktop::RgbaImage& image) {
  std::string bytes("\x89PNG\r\n\x1a\n", 8);
  std::string header;
  AppendBigEndian(&header, static_cast<std::uint32_t>(image.size.width));
  AppendBigEndian(&header, static_cast<std::uint32_t>(image.size.height));
  header.append("\x08\x02\x00\x00\x00", 5);
  AppendChunk(&bytes, "IHDR", header);
  std::string raw;
  const std::size_t count = static_cast<std::size_t>(image.size.width) *
                            static_cast<std::size_t>(image.size.height);
  raw.reserve((static_cast<std::size_t>(image.size.width) * 3 + 1) *
              static_cast<std::size_t>(image.size.height));
  for (std::size_t pixel = 0; pixel < count; ++pixel) {
    if (pixel % static_cast<std::size_t>(image.size.width) == 0) {
      raw.push_back('\0');
    }
    for (std::size_t channel = 0; channel < 3; ++channel) {
      raw.push_back(static_cast<char>(image.pixels.at((pixel * 4) + channel)));
    }
  }
  uLongf compressed_size = compressBound(static_cast<uLong>(raw.size()));
  std::string compressed(compressed_size, '\0');
  if (compress2(reinterpret_cast<Bytef*>(compressed.data()), &compressed_size,
                reinterpret_cast<const Bytef*>(raw.data()),
                static_cast<uLong>(raw.size()), Z_BEST_COMPRESSION) != Z_OK) {
    return {};
  }
  compressed.resize(compressed_size);
  AppendChunk(&bytes, "IDAT", compressed);
  AppendChunk(&bytes, "IEND", {});
  return bytes;
}

inline std::uint32_t ReadBigEndian(std::string_view bytes, std::size_t offset) {
  return (static_cast<std::uint32_t>(
              static_cast<unsigned char>(bytes.at(offset)))
          << 24) |
         (static_cast<std::uint32_t>(
              static_cast<unsigned char>(bytes.at(offset + 1)))
          << 16) |
         (static_cast<std::uint32_t>(
              static_cast<unsigned char>(bytes.at(offset + 2)))
          << 8) |
         static_cast<std::uint32_t>(
             static_cast<unsigned char>(bytes.at(offset + 3)));
}

inline std::optional<penumbra::desktop::RgbaImage> FromPng(
    const std::string& bytes) {
  if (bytes.size() < 33 || bytes.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) != 0 ||
      bytes.compare(12, 4, "IHDR", 4) != 0) {
    return std::nullopt;
  }
  const std::uint32_t width = ReadBigEndian(bytes, 16);
  const std::uint32_t height = ReadBigEndian(bytes, 20);
  if (width == 0 || height == 0 ||
      static_cast<unsigned char>(bytes.at(24)) != 8 ||
      static_cast<unsigned char>(bytes.at(25)) != 2 || bytes.at(28) != 0) {
    return std::nullopt;
  }
  std::string compressed;
  for (std::size_t offset = 8; offset + 12 <= bytes.size();) {
    const std::uint32_t length = ReadBigEndian(bytes, offset);
    if (length > bytes.size() - offset - 12) {
      return std::nullopt;
    }
    const std::string_view type = std::string_view(bytes).substr(offset + 4, 4);
    if (type == "IDAT") {
      compressed.append(bytes, offset + 8, length);
    }
    offset += static_cast<std::size_t>(length) + 12;
    if (type == "IEND") {
      break;
    }
  }
  const uLongf raw_size = (static_cast<uLongf>(width) * 3 + 1) * height;
  std::string raw(raw_size, '\0');
  uLongf actual_size = raw_size;
  if (uncompress(reinterpret_cast<Bytef*>(raw.data()), &actual_size,
                 reinterpret_cast<const Bytef*>(compressed.data()),
                 static_cast<uLong>(compressed.size())) != Z_OK ||
      actual_size != raw_size) {
    return std::nullopt;
  }
  penumbra::desktop::RgbaImage image;
  image.size = penumbra::desktop::PixelSize{.width = static_cast<int>(width),
                                            .height = static_cast<int>(height)};
  const std::size_t count = static_cast<std::size_t>(width) * height;
  image.pixels.assign(count * 4, 255);
  for (std::size_t pixel = 0; pixel < count; ++pixel) {
    const std::size_t row = pixel / width;
    if (pixel % width == 0 &&
        raw.at(row * (static_cast<std::size_t>(width) * 3 + 1)) != 0) {
      return std::nullopt;
    }
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const std::size_t source =
          (row * (static_cast<std::size_t>(width) * 3 + 1)) + 1 +
          ((pixel % width) * 3) + channel;
      image.pixels.at((pixel * 4) + channel) =
          static_cast<std::uint8_t>(raw.at(source));
    }
  }
  return image;
}

inline std::optional<std::string> ReadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

inline bool WriteFile(const std::string& path, const std::string& bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(file);
}

inline Comparison Compare(const penumbra::desktop::RgbaImage& actual,
                          const penumbra::desktop::RgbaImage& expected,
                          int per_pixel_threshold) {
  Comparison result;
  const std::size_t count = static_cast<std::size_t>(actual.size.width) *
                            static_cast<std::size_t>(actual.size.height);
  double total = 0;
  std::size_t exceeding = 0;
  for (std::size_t pixel = 0; pixel < count; ++pixel) {
    int worst = 0;
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const std::size_t index = (pixel * 4) + channel;
      const int difference =
          std::abs(static_cast<int>(actual.pixels.at(index)) -
                   static_cast<int>(expected.pixels.at(index)));
      worst = std::max(worst, difference);
      total += difference;
    }
    result.max_difference = std::max(result.max_difference, worst);
    if (worst > per_pixel_threshold) {
      ++exceeding;
    }
  }
  result.mean_difference = total / static_cast<double>(count * 3);
  result.exceeding_fraction =
      static_cast<double>(exceeding) / static_cast<double>(count);
  return result;
}

}

#endif
