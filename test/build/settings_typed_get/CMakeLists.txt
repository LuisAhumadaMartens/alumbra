cmake_minimum_required(VERSION 3.28)
project(SettingsTypedGet LANGUAGES CXX)
find_package(absl CONFIG REQUIRED)
add_library(settings_typed_get OBJECT usage.cc)
target_compile_features(settings_typed_get PRIVATE cxx_std_20)
target_include_directories(settings_typed_get PRIVATE
  "${ALUMBRA_SOURCE}/penumbra/interface/include"
  "${ALUMBRA_SOURCE}/umbra/base/include"
  "${ALUMBRA_SOURCE}/umbra/base/internal_include"
  "${ALUMBRA_SOURCE}/umbra/settings/internal_include")
target_link_libraries(settings_typed_get PRIVATE
  absl::any_invocable absl::status absl::statusor absl::time)
target_compile_options(settings_typed_get PRIVATE -fno-exceptions)
if(ALUMBRA_WRONG_TYPE)
  target_compile_definitions(settings_typed_get PRIVATE ALUMBRA_WRONG_TYPE)
endif()
