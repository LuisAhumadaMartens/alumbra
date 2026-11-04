find_package(SDL3 CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
include("${PROJECT_SOURCE_DIR}/cmake/AlumbraEmbedBlob.cmake")

set(_font_source "${CMAKE_CURRENT_BINARY_DIR}/embedded_font.cc")
alumbra_embed_blob("${_font_source}" "alumbra/desktop_ui/embedded_font.h"
  "alumbra::desktop_ui" EmbeddedFontData
  "${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Regular.ttf")

add_library(antumbra_desktop_ui STATIC
  animation.cc
  compose.cc
  desktop_ui.cc
  desktop_window.cc
  draw_list.cc
  font_face.cc
  glyph_atlas.cc
  gpu_renderer.cc
  icons.cc
  input_translator.cc
  layout.cc
  strings.cc
  style.cc
  text_shaper.cc
  "${_font_source}")
add_library(alumbra::desktop_ui ALIAS antumbra_desktop_ui)
set_target_properties(antumbra_desktop_ui PROPERTIES
  EXPORT_NAME "alumbra::desktop_ui")
target_include_directories(antumbra_desktop_ui PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(antumbra_desktop_ui
  PUBLIC
    alumbra::design_tokens
    umbra::player
    umbra::ui
    penumbra::desktop_port
    SDL3::SDL3
    absl::status
    absl::statusor
  PRIVATE
    Freetype::Freetype
    harfbuzz::harfbuzz
    absl::strings)
alumbra_target_options(antumbra_desktop_ui)
install(TARGETS antumbra_desktop_ui EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
