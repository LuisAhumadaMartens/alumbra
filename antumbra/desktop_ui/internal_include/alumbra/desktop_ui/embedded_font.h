#ifndef ALUMBRA_DESKTOP_UI_EMBEDDED_FONT_H_
#define ALUMBRA_DESKTOP_UI_EMBEDDED_FONT_H_

#include <cstdint>
#include <span>

namespace alumbra::desktop_ui {

std::span<const std::uint8_t> EmbeddedFontData();

}

#endif
