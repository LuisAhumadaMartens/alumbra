#include <version>

#define ALUMBRA_STRINGIFY_IMPL(x) #x
#define ALUMBRA_STRINGIFY(x) ALUMBRA_STRINGIFY_IMPL(x)

#if defined(_LIBCPP_VERSION)
#define ALUMBRA_STDLIB "libc++"
#define ALUMBRA_STDLIB_VERSION ALUMBRA_STRINGIFY(_LIBCPP_VERSION)
#elif defined(_GLIBCXX_RELEASE)
#define ALUMBRA_STDLIB "libstdc++"
#define ALUMBRA_STDLIB_VERSION ALUMBRA_STRINGIFY(_GLIBCXX_RELEASE)
#else
#define ALUMBRA_STDLIB "unknown"
#define ALUMBRA_STDLIB_VERSION "0"
#endif

#if defined(__ENVIRONMENT_OS_VERSION_MIN_REQUIRED__)
#define ALUMBRA_MIN_OS \
  ALUMBRA_STRINGIFY(__ENVIRONMENT_OS_VERSION_MIN_REQUIRED__)
#elif defined(__ANDROID_API__)
#define ALUMBRA_MIN_OS ALUMBRA_STRINGIFY(__ANDROID_API__)
#else
#define ALUMBRA_MIN_OS ""
#endif

extern const char kAlumbraToolchainInfo[];
const char kAlumbraToolchainInfo[] =
    "ALUMBRA_TOOLCHAIN_INFO|stdlib=" ALUMBRA_STDLIB
    "|stdlib_version=" ALUMBRA_STDLIB_VERSION
    "|cplusplus=" ALUMBRA_STRINGIFY(__cplusplus) "|min_os=" ALUMBRA_MIN_OS "|";
