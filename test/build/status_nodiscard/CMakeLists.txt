cmake_minimum_required(VERSION 3.28)
project(StatusUsage LANGUAGES CXX)
find_package(absl CONFIG REQUIRED)
add_library(status_usage OBJECT usage.cc)
target_compile_features(status_usage PRIVATE cxx_std_20)
target_include_directories(status_usage PRIVATE
  "${ALUMBRA_SOURCE}/umbra/base/include"
  "${ALUMBRA_SOURCE}/umbra/base/internal_include")
target_link_libraries(status_usage PRIVATE absl::status absl::statusor)
target_compile_options(status_usage PRIVATE -Werror=unused-result -fno-exceptions)
if(ALUMBRA_IGNORE_STATUS)
  target_compile_definitions(status_usage PRIVATE ALUMBRA_IGNORE_STATUS)
endif()
