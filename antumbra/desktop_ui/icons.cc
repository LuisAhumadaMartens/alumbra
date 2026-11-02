#include "alumbra/desktop_ui/icons.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#include "alumbra/desktop_ui/geometry.h"
#include "alumbra/desktop_ui/structure.h"

namespace alumbra::desktop_ui {
namespace {

enum class ShapeKind { kPolygon, kCapsule, kCircle, kRing };

struct Shape {
  ShapeKind kind = ShapeKind::kCircle;
  std::vector<Point> points;
  float outer = 0;
  float inner = 0;
};

Shape Polygon(std::vector<Point> points) {
  Shape shape;
  shape.kind = ShapeKind::kPolygon;
  shape.points = std::move(points);
  return shape;
}

Shape Rectangle(float left, float top, float right, float bottom) {
  return Polygon({{left, top}, {right, top}, {right, bottom}, {left, bottom}});
}

Shape Capsule(Point from, Point to, float radius) {
  Shape shape;
  shape.kind = ShapeKind::kCapsule;
  shape.points = {from, to};
  shape.outer = radius;
  return shape;
}

Shape Circle(Point centre, float radius) {
  Shape shape;
  shape.kind = ShapeKind::kCircle;
  shape.points = {centre};
  shape.outer = radius;
  return shape;
}

Shape Ring(Point centre, float outer, float inner) {
  Shape shape;
  shape.kind = ShapeKind::kRing;
  shape.points = {centre};
  shape.outer = outer;
  shape.inner = inner;
  return shape;
}

void AddArc(std::vector<Shape>& shapes, Point centre, float radius,
            float from_degrees, float to_degrees, float half_width) {
  constexpr int kSegments = 6;
  const float degrees_to_radians = kPi / 180.0F;
  for (int segment = 0; segment < kSegments; ++segment) {
    const auto fraction = [&](int index) {
      return from_degrees + ((to_degrees - from_degrees) *
                             static_cast<float>(index) / kSegments);
    };
    const float a0 = fraction(segment) * degrees_to_radians;
    const float a1 = fraction(segment + 1) * degrees_to_radians;
    shapes.push_back(Capsule(Point{centre.x + (radius * std::cos(a0)),
                                   centre.y + (radius * std::sin(a0))},
                             Point{centre.x + (radius * std::cos(a1)),
                                   centre.y + (radius * std::sin(a1))},
                             half_width));
  }
}

std::vector<Shape> SpeakerBody() {
  std::vector<Shape> shapes;
  shapes.push_back(
      Polygon({{4, 9}, {8, 9}, {13, 5}, {13, 19}, {8, 15}, {4, 15}}));
  return shapes;
}

std::vector<Shape> ShapesOf(IconId icon) {
  std::vector<Shape> shapes;
  switch (icon) {
    case IconId::kPlay:
      shapes.push_back(Polygon({{8, 5}, {8, 19}, {19, 12}}));
      break;
    case IconId::kPause:
      shapes.push_back(Rectangle(6, 5, 10, 19));
      shapes.push_back(Rectangle(14, 5, 18, 19));
      break;
    case IconId::kVolumeOn:
      shapes = SpeakerBody();
      AddArc(shapes, Point{13, 12}, 5, -45, 45, 1);
      AddArc(shapes, Point{13, 12}, 8.5F, -45, 45, 1);
      break;
    case IconId::kVolumeOff:
      shapes = SpeakerBody();
      shapes.push_back(Capsule(Point{16, 9}, Point{21, 15}, 1));
      shapes.push_back(Capsule(Point{21, 9}, Point{16, 15}, 1));
      break;
    case IconId::kFullscreen:
      shapes.push_back(Capsule(Point{4, 4}, Point{4, 9}, 1));
      shapes.push_back(Capsule(Point{4, 4}, Point{9, 4}, 1));
      shapes.push_back(Capsule(Point{20, 4}, Point{20, 9}, 1));
      shapes.push_back(Capsule(Point{20, 4}, Point{15, 4}, 1));
      shapes.push_back(Capsule(Point{4, 20}, Point{4, 15}, 1));
      shapes.push_back(Capsule(Point{4, 20}, Point{9, 20}, 1));
      shapes.push_back(Capsule(Point{20, 20}, Point{20, 15}, 1));
      shapes.push_back(Capsule(Point{20, 20}, Point{15, 20}, 1));
      break;
    case IconId::kFullscreenExit:
      shapes.push_back(Capsule(Point{9, 4}, Point{9, 9}, 1));
      shapes.push_back(Capsule(Point{9, 9}, Point{4, 9}, 1));
      shapes.push_back(Capsule(Point{15, 4}, Point{15, 9}, 1));
      shapes.push_back(Capsule(Point{15, 9}, Point{20, 9}, 1));
      shapes.push_back(Capsule(Point{9, 20}, Point{9, 15}, 1));
      shapes.push_back(Capsule(Point{9, 15}, Point{4, 15}, 1));
      shapes.push_back(Capsule(Point{15, 20}, Point{15, 15}, 1));
      shapes.push_back(Capsule(Point{15, 15}, Point{20, 15}, 1));
      break;
    case IconId::kSettings: {
      shapes.push_back(Ring(Point{12, 12}, 7, 3.5F));
      constexpr int kTeeth = 6;
      for (int tooth = 0; tooth < kTeeth; ++tooth) {
        const float angle = static_cast<float>(tooth) * kPi / 3.0F;
        shapes.push_back(Capsule(
            Point{12 + (6.5F * std::cos(angle)), 12 + (6.5F * std::sin(angle))},
            Point{12 + (9.5F * std::cos(angle)), 12 + (9.5F * std::sin(angle))},
            1.6F));
      }
      break;
    }
    case IconId::kSkipAd:
      shapes.push_back(Polygon({{5, 5}, {5, 19}, {14, 12}}));
      shapes.push_back(Rectangle(16, 5, 19, 19));
      break;
    case IconId::kError:
      shapes.push_back(Ring(Point{12, 12}, 10, 8));
      shapes.push_back(Capsule(Point{12, 7}, Point{12, 13}, 1.2F));
      shapes.push_back(Circle(Point{12, 17}, 1.4F));
      break;
    case IconId::kDot:
      shapes.push_back(Circle(Point{12, 12}, 4));
      break;
  }
  return shapes;
}

float SegmentDistance(Point point, Point from, Point to) {
  const float dx = to.x - from.x;
  const float dy = to.y - from.y;
  const float length_squared = (dx * dx) + (dy * dy);
  float along = 0;
  if (length_squared > 0) {
    along = (((point.x - from.x) * dx) + ((point.y - from.y) * dy)) /
            length_squared;
  }
  along = std::clamp(along, 0.0F, 1.0F);
  const float nearest_x = from.x + (along * dx);
  const float nearest_y = from.y + (along * dy);
  return std::hypot(point.x - nearest_x, point.y - nearest_y);
}

float PolygonSignedDistance(const std::vector<Point>& points, Point point) {
  float distance = std::numeric_limits<float>::max();
  bool inside = false;
  const std::size_t count = points.size();
  for (std::size_t index = 0; index < count; ++index) {
    const Point& from = points.at(index);
    const Point& to = points.at((index + 1) % count);
    distance = std::min(distance, SegmentDistance(point, from, to));
    const bool crosses = (from.y > point.y) != (to.y > point.y);
    if (crosses) {
      const float x_at =
          from.x + ((point.y - from.y) / (to.y - from.y) * (to.x - from.x));
      if (point.x < x_at) {
        inside = !inside;
      }
    }
  }
  return inside ? distance : -distance;
}

float SignedDistance(const Shape& shape, Point point) {
  switch (shape.kind) {
    case ShapeKind::kPolygon:
      return PolygonSignedDistance(shape.points, point);
    case ShapeKind::kCapsule:
      return shape.outer -
             SegmentDistance(point, shape.points.at(0), shape.points.at(1));
    case ShapeKind::kCircle:
      return shape.outer - std::hypot(point.x - shape.points.at(0).x,
                                      point.y - shape.points.at(0).y);
    case ShapeKind::kRing: {
      const float radius = std::hypot(point.x - shape.points.at(0).x,
                                      point.y - shape.points.at(0).y);
      return std::min(shape.outer - radius, radius - shape.inner);
    }
  }
  return -std::numeric_limits<float>::max();
}

std::uint8_t Encode(float distance_units) {
  const float units_to_pixels =
      static_cast<float>(kIconGlyphSize) / kIconGridUnits;
  const float distance_pixels = distance_units * units_to_pixels;
  const float value =
      std::clamp(0.5F + (distance_pixels / (2 * kIconSdfSpread)), 0.0F, 1.0F);
  return static_cast<std::uint8_t>(std::lround(value * 255.0F));
}

}

IconId IconFromToken(std::string_view token) {
  struct Entry {
    std::string_view token;
    IconId icon;
  };
  static constexpr std::array<Entry, 10> kEntries{{
      {.token = "icon.play", .icon = IconId::kPlay},
      {.token = "icon.pause", .icon = IconId::kPause},
      {.token = "icon.volume", .icon = IconId::kVolumeOn},
      {.token = "icon.volume_on", .icon = IconId::kVolumeOn},
      {.token = "icon.volume_off", .icon = IconId::kVolumeOff},
      {.token = "icon.fullscreen", .icon = IconId::kFullscreen},
      {.token = "icon.fullscreen_enter", .icon = IconId::kFullscreen},
      {.token = "icon.fullscreen_exit", .icon = IconId::kFullscreenExit},
      {.token = "icon.settings", .icon = IconId::kSettings},
      {.token = "icon.skip_ad", .icon = IconId::kSkipAd},
  }};
  for (const Entry& entry : kEntries) {
    if (entry.token == token) {
      return entry.icon;
    }
  }
  return IconId::kDot;
}

IconSdfAtlas::IconSdfAtlas()
    : pixels_(static_cast<std::size_t>(kIconAtlasSize) * kIconAtlasSize, 0) {
  const float margin = static_cast<float>(kIconCellSize - kIconGlyphSize) / 2;
  const float units_to_pixels =
      static_cast<float>(kIconGlyphSize) / kIconGridUnits;
  for (std::size_t index = 0; index < kIconCount; ++index) {
    const std::vector<Shape> shapes = ShapesOf(static_cast<IconId>(index));
    const int origin_x =
        static_cast<int>(index % kIconCellsPerRow) * kIconCellSize;
    const int origin_y =
        static_cast<int>(index / kIconCellsPerRow) * kIconCellSize;
    for (int row = 0; row < kIconCellSize; ++row) {
      for (int column = 0; column < kIconCellSize; ++column) {
        const Point point{
            ((static_cast<float>(column) + 0.5F) - margin) / units_to_pixels,
            ((static_cast<float>(row) + 0.5F) - margin) / units_to_pixels};
        float best = -std::numeric_limits<float>::max();
        for (const Shape& shape : shapes) {
          best = std::max(best, SignedDistance(shape, point));
        }
        const std::size_t offset =
            (static_cast<std::size_t>(origin_y + row) * kIconAtlasSize) +
            static_cast<std::size_t>(origin_x + column);
        pixels_.at(offset) = Encode(best);
      }
    }
  }
}

IconAtlasCell IconSdfAtlas::Cell(IconId icon) {
  const int index = static_cast<int>(icon);
  const auto atlas = static_cast<float>(kIconAtlasSize);
  const int column_index = index % kIconCellsPerRow;
  const int row_index = index / kIconCellsPerRow;
  const auto column = static_cast<float>(column_index);
  const auto row = static_cast<float>(row_index);
  const auto cell = static_cast<float>(kIconCellSize);
  return IconAtlasCell{.u0 = column * cell / atlas,
                       .v0 = row * cell / atlas,
                       .u1 = (column + 1) * cell / atlas,
                       .v1 = (row + 1) * cell / atlas};
}

float IconSdfAtlas::CellScale() {
  return static_cast<float>(kIconCellSize) / static_cast<float>(kIconGlyphSize);
}

}
