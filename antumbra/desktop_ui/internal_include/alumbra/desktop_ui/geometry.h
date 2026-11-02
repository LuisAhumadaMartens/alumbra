#ifndef ALUMBRA_DESKTOP_UI_GEOMETRY_H_
#define ALUMBRA_DESKTOP_UI_GEOMETRY_H_

#include <algorithm>

#include "alumbra/design_tokens/design_tokens.h"

namespace alumbra::desktop_ui {

struct Point {
  constexpr Point() = default;
  constexpr Point(float px, float py) : x(px), y(py) {}
  float x = 0;
  float y = 0;
  friend bool operator==(const Point&, const Point&) = default;
};

struct Size {
  constexpr Size() = default;
  constexpr Size(float w, float h) : width(w), height(h) {}
  float width = 0;
  float height = 0;
  friend bool operator==(const Size&, const Size&) = default;
};

struct Rect {
  constexpr Rect() = default;
  constexpr Rect(float rx, float ry, float w, float h)
      : x(rx), y(ry), width(w), height(h) {}
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;

  [[nodiscard]] float Right() const { return x + width; }
  [[nodiscard]] float Bottom() const { return y + height; }
  [[nodiscard]] Point Centre() const {
    return Point{x + (width / 2), y + (height / 2)};
  }
  [[nodiscard]] bool Empty() const { return width <= 0 || height <= 0; }
  [[nodiscard]] bool Contains(Point point) const {
    return point.x >= x && point.x < Right() && point.y >= y &&
           point.y < Bottom();
  }
  [[nodiscard]] Rect Inflated(float amount) const {
    return Rect{x - amount, y - amount, width + (2 * amount),
                height + (2 * amount)};
  }
  [[nodiscard]] Rect Intersected(const Rect& other) const {
    const float left = std::max(x, other.x);
    const float top = std::max(y, other.y);
    const float right = std::min(Right(), other.Right());
    const float bottom = std::min(Bottom(), other.Bottom());
    return Rect{left, top, std::max(0.0F, right - left),
                std::max(0.0F, bottom - top)};
  }
  friend bool operator==(const Rect&, const Rect&) = default;
};

struct Rgba {
  constexpr Rgba() = default;
  constexpr Rgba(float r, float g, float b, float a)
      : red(r), green(g), blue(b), alpha(a) {}
  float red = 0;
  float green = 0;
  float blue = 0;
  float alpha = 0;
  friend bool operator==(const Rgba&, const Rgba&) = default;
};

[[nodiscard]] inline Rgba FromToken(const design_tokens::Color& color) {
  return Rgba{
      static_cast<float>(color.srgb.red), static_cast<float>(color.srgb.green),
      static_cast<float>(color.srgb.blue), static_cast<float>(color.alpha)};
}

[[nodiscard]] inline Rgba Scaled(const Rgba& color, float alpha_factor) {
  return Rgba{color.red, color.green, color.blue, color.alpha * alpha_factor};
}

}

#endif
