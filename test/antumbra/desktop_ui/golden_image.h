#ifndef TEST_ANTUMBRA_DESKTOP_UI_GOLDEN_IMAGE_H_
#define TEST_ANTUMBRA_DESKTOP_UI_GOLDEN_IMAGE_H_

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
#include <vector>

#include "penumbra/desktop/video/presenter.h"

namespace alumbra::desktop_ui::testing {

struct Comparison {
  int max_difference = 0;
  double mean_difference = 0;
  double exceeding_fraction = 0;
};

inline std::string ToPpm(const penumbra::desktop::RgbaImage& image) {
  std::ostringstream out(std::ios::binary);
  out << "P6\n" << image.size.width << " " << image.size.height << "\n255\n";
  const std::size_t count = static_cast<std::size_t>(image.size.width) *
                            static_cast<std::size_t>(image.size.height);
  for (std::size_t pixel = 0; pixel < count; ++pixel) {
    for (std::size_t channel = 0; channel < 3; ++channel) {
      out.put(static_cast<char>(image.pixels.at((pixel * 4) + channel)));
    }
  }
  return out.str();
}

inline std::optional<penumbra::desktop::RgbaImage> FromPpm(
    const std::string& bytes) {
  std::istringstream in(bytes, std::ios::binary);
  std::string magic;
  int width = 0;
  int height = 0;
  int maximum = 0;
  in >> magic >> width >> height >> maximum;
  if (magic != "P6" || width <= 0 || height <= 0 || maximum != 255) {
    return std::nullopt;
  }
  in.get();
  penumbra::desktop::RgbaImage image;
  image.size = penumbra::desktop::PixelSize{.width = width, .height = height};
  const std::size_t count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  image.pixels.assign(count * 4, 255);
  for (std::size_t pixel = 0; pixel < count; ++pixel) {
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const int value = in.get();
      if (value < 0) {
        return std::nullopt;
      }
      image.pixels.at((pixel * 4) + channel) = static_cast<std::uint8_t>(value);
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
