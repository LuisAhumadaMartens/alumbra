cmake_minimum_required(VERSION 3.28)
project(AlumbraErrorFuzz LANGUAGES CXX)
find_package(absl CONFIG REQUIRED)
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang$")
  message(FATAL_ERROR "Error fuzz targets require Clang with libFuzzer.")
endif()
get_filename_component(_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(error_detail_fuzz status_fuzz.cc
  "${_root}/umbra/base/status.cc"
  "${_root}/umbra/base/url_redactor.cc"
  "${_root}/umbra/base/sip_hash.cc")
target_include_directories(error_detail_fuzz PRIVATE
  "${_root}/umbra/base/include" "${_root}/umbra/base/internal_include")
target_compile_features(error_detail_fuzz PRIVATE cxx_std_23)
target_link_libraries(error_detail_fuzz PRIVATE absl::status absl::statusor
  absl::cord)
target_compile_options(error_detail_fuzz PRIVATE
  -Wall -Wextra -Wpedantic -Werror -fno-exceptions
  -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all)
set(ALUMBRA_LIBFUZZER_LIBRARY "" CACHE FILEPATH
  "External libFuzzer driver when the compiler does not ship one")
target_link_options(error_detail_fuzz PRIVATE -fsanitize=address,undefined)
if(ALUMBRA_LIBFUZZER_LIBRARY)
  target_link_libraries(error_detail_fuzz PRIVATE "${ALUMBRA_LIBFUZZER_LIBRARY}")
else()
  target_link_options(error_detail_fuzz PRIVATE -fsanitize=fuzzer)
endif()
