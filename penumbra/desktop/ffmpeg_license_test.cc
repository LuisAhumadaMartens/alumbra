#include <dlfcn.h>

#include <array>
#include <string>
#include <string_view>

#include "gtest/gtest.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libswresample/swresample.h"
}

namespace penumbra::desktop {
namespace {

constexpr const char* kLgpl21OrLater = "LGPL version 2.1 or later";

struct FfmpegLibrary {
  std::string_view name;
  const void* symbol;
};

std::string ImageFileName(const void* symbol) {
  Dl_info info{};
  if (dladdr(symbol, &info) == 0 || info.dli_fname == nullptr) {
    return {};
  }
  const std::string_view path = info.dli_fname;
  return std::string(path.substr(path.find_last_of('/') + 1));
}

bool IsSharedLibraryFile(std::string_view file, std::string_view library) {
  const std::string prefix = "lib" + std::string(library) + ".";
  return file.starts_with(prefix) &&
         (file.ends_with(".dylib") || file.find(".so") != std::string::npos);
}

TEST(FfmpegLicenseTest, LibrariesReportLgpl21OrLater) {
  EXPECT_STREQ(avcodec_license(), kLgpl21OrLater);
  EXPECT_STREQ(avformat_license(), kLgpl21OrLater);
  EXPECT_STREQ(avutil_license(), kLgpl21OrLater);
  EXPECT_STREQ(swresample_license(), kLgpl21OrLater);
}

TEST(FfmpegLicenseTest, LibrariesAreSharedObjects) {
  const std::array<FfmpegLibrary, 4> libraries = {{
      {.name="avcodec", .symbol=reinterpret_cast<const void*>(&avcodec_license)},
      {.name="avformat", .symbol=reinterpret_cast<const void*>(&avformat_license)},
      {.name="avutil", .symbol=reinterpret_cast<const void*>(&avutil_license)},
      {.name="swresample", .symbol=reinterpret_cast<const void*>(&swresample_license)},
  }};
  for (const FfmpegLibrary& library : libraries) {
    const std::string file = ImageFileName(library.symbol);
    EXPECT_TRUE(IsSharedLibraryFile(file, library.name))
        << library.name << " resolved to '" << file << "'";
  }
}

}
}
