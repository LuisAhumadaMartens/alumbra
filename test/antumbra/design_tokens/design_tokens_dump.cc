#include <iostream>
#include <limits>
#include <ostream>
#include <string_view>

#include "alumbra/design_tokens/design_tokens.h"

namespace {

namespace tokens = alumbra::design_tokens;

void WriteString(std::ostream& out, std::string_view value) {
  out << '"';
  for (const char character : value) {
    if (character == '"' || character == '\\') {
      out << '\\';
    }
    out << character;
  }
  out << '"';
}

void WriteRgb(std::ostream& out, const tokens::Rgb& value) {
  out << '[' << value.red << ',' << value.green << ',' << value.blue << ']';
}

void WriteValue(std::ostream& out, const tokens::Color& value) {
  out << R"({"colorSpace":)";
  switch (value.color_space) {
    case tokens::ColorSpace::kSrgb:
      WriteString(out, "srgb");
      break;
    case tokens::ColorSpace::kDisplayP3:
      WriteString(out, "display-p3");
      break;
    case tokens::ColorSpace::kRec2020:
      WriteString(out, "rec2020");
      break;
  }
  out << R"(,"components":)";
  WriteRgb(out, value.components);
  out << R"(,"alpha":)" << value.alpha << R"(,"srgb":)";
  WriteRgb(out, value.srgb);
  out << '}';
}

void WriteValue(std::ostream& out, double value) { out << value; }

void WriteValue(std::ostream& out, tokens::Milliseconds value) {
  out << value.count();
}

void WriteValue(std::ostream& out, const tokens::CubicBezier& value) {
  out << '[' << value.x1 << ',' << value.y1 << ',' << value.x2 << ','
      << value.y2 << ']';
}

void WriteValue(std::ostream& out, tokens::FontFamily value) {
  out << '[';
  std::string_view separator;
  for (const std::string_view name : value) {
    out << separator;
    WriteString(out, name);
    separator = ",";
  }
  out << ']';
}

void WriteValue(std::ostream& out, tokens::Shadow value) {
  out << '[';
  std::string_view separator;
  for (const tokens::ShadowLayer& layer : value) {
    out << separator << R"({"color":)";
    WriteValue(out, layer.color);
    out << R"(,"offsetX":)" << layer.offset_x << R"(,"offsetY":)"
        << layer.offset_y << R"(,"blur":)" << layer.blur << R"(,"spread":)"
        << layer.spread << R"(,"inset":)" << (layer.inset ? "true" : "false")
        << '}';
    separator = ",";
  }
  out << ']';
}

void WriteTokens(std::ostream& out, const tokens::Tokens& values) {
  out << '{';
  std::string_view separator;
  auto visitor = [&out, &separator](std::string_view name, const auto& value) {
    out << separator;
    WriteString(out, name);
    out << ':';
    WriteValue(out, value);
    separator = ",";
  };
  tokens::VisitTokens(values, visitor);
  out << '}';
}

tokens::Color Opaque(double red, double green, double blue, double alpha) {
  const tokens::Rgb components{.red = red, .green = green, .blue = blue};
  return {.color_space = tokens::ColorSpace::kSrgb,
          .components = components,
          .alpha = alpha,
          .srgb = components};
}

void WriteContexts(std::ostream& out) {
  std::string_view separator;
  for (const tokens::Theme theme : tokens::kThemeValues) {
    for (const tokens::Contrast contrast : tokens::kContrastValues) {
      for (const tokens::Density density : tokens::kDensityValues) {
        for (const tokens::Motion motion : tokens::kMotionValues) {
          for (const tokens::DynamicRange range : tokens::kDynamicRangeValues) {
            out << separator << '"' << tokens::Name(theme) << '/'
                << tokens::Name(contrast) << '/' << tokens::Name(density) << '/'
                << tokens::Name(motion) << '/' << tokens::Name(range)
                << R"(":)";
            WriteTokens(out, tokens::Resolve({.theme = theme,
                                              .contrast = contrast,
                                              .density = density,
                                              .motion = motion,
                                              .dynamic_range = range}));
            separator = ",";
          }
        }
      }
    }
  }
}

}

int main() {
  std::cout.precision(std::numeric_limits<double>::max_digits10);
  tokens::Overrides semantic;
  semantic.alumbra_token_color_scontrol_sbackground =
      Opaque(0.25, 0.5, 0.75, 1.0);
  tokens::Overrides component = semantic;
  component.alumbra_token_player_sbutton_sbackground_sfocus =
      Opaque(0.125, 0.25, 0.375, 0.5);
  std::cout << R"({"language":"cpp","contexts":{)";
  WriteContexts(std::cout);
  std::cout << R"(},"scenarios":{"semantic":)";
  WriteTokens(std::cout, tokens::Resolve({}, semantic));
  std::cout << R"(,"component":)";
  WriteTokens(std::cout, tokens::Resolve({}, component));
  std::cout << "}}\n";
  return 0;
}
