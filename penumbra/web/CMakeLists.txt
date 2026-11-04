add_library(penumbra_web_http STATIC fetch_http_transport.cc)
add_library(penumbra::web_http ALIAS penumbra_web_http)
target_include_directories(penumbra_web_http PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>")
target_link_libraries(penumbra_web_http
  PUBLIC penumbra::interface
  PRIVATE absl::strings absl::time)
target_link_options(penumbra_web_http INTERFACE
  "SHELL:--pre-js ${PROJECT_SOURCE_DIR}/web/fetch_transport.mjs")
target_compile_options(penumbra_web_http PRIVATE -fno-exceptions)
alumbra_target_options(penumbra_web_http)
