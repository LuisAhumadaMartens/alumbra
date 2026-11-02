func string(_ value: String) -> String {
  var result = "\""
  for scalar in value.unicodeScalars {
    switch scalar {
    case "\"": result += "\\\""
    case "\\": result += "\\\\"
    case let control where control.value < 0x20:
      result += "\\u" + String(repeating: "0", count: 4 - String(control.value, radix: 16).count)
        + String(control.value, radix: 16)
    default: result.unicodeScalars.append(scalar)
    }
  }
  return result + "\""
}

func number(_ value: Double) -> String {
  "\(value)"
}

func rgb(_ value: DesignTokens.Rgb) -> String {
  "[\(number(value.red)),\(number(value.green)),\(number(value.blue))]"
}

func color(_ value: DesignTokens.Color) -> String {
  "{\"colorSpace\":\(string(value.colorSpace.rawValue)),\"components\":\(rgb(value.components)),"
    + "\"alpha\":\(number(value.alpha)),\"srgb\":\(rgb(value.srgb))}"
}

func layer(_ value: DesignTokens.ShadowLayer) -> String {
  "{\"color\":\(color(value.color)),\"offsetX\":\(number(value.offsetX)),"
    + "\"offsetY\":\(number(value.offsetY)),\"blur\":\(number(value.blur)),"
    + "\"spread\":\(number(value.spread)),\"inset\":\(value.inset)}"
}

func canonical(_ value: DesignTokens.Value) -> String {
  switch value {
  case .color(let value): color(value)
  case .dimension(let value), .duration(let value), .number(let value), .fontWeight(let value):
    number(value)
  case .fontFamily(let names): "[" + names.map(string).joined(separator: ",") + "]"
  case .cubicBezier(let curve):
    "[\(number(curve.x1)),\(number(curve.y1)),\(number(curve.x2)),\(number(curve.y2))]"
  case .shadow(let layers): "[" + layers.map(layer).joined(separator: ",") + "]"
  }
}

func dump(_ tokens: DesignTokens.Tokens) -> String {
  "{" + tokens.entries.map { "\(string($0.name)):\(canonical($0.value))" }
    .joined(separator: ",") + "}"
}

func srgb(_ red: Double, _ green: Double, _ blue: Double, _ alpha: Double) -> DesignTokens.Color {
  let components = DesignTokens.Rgb(red: red, green: green, blue: blue)
  return DesignTokens.Color(colorSpace: .srgb, components: components, alpha: alpha, srgb: components)
}

var contexts: [String] = []
for theme in DesignTokens.Theme.allCases {
  for contrast in DesignTokens.Contrast.allCases {
    for density in DesignTokens.Density.allCases {
      for motion in DesignTokens.Motion.allCases {
        for dynamicRange in DesignTokens.DynamicRange.allCases {
          let context = DesignTokens.Context(
            theme: theme, contrast: contrast, density: density, motion: motion,
            dynamicRange: dynamicRange)
          let key = [
            theme.rawValue, contrast.rawValue, density.rawValue, motion.rawValue,
            dynamicRange.rawValue,
          ].joined(separator: "/")
          contexts.append("\(string(key)):\(dump(DesignTokens.Tokens(context: context)))")
        }
      }
    }
  }
}

var semantic = DesignTokens.Overrides()
semantic.alumbra_token_color_scontrol_sbackground = srgb(0.25, 0.5, 0.75, 1)
var component = semantic
component.alumbra_token_player_sbutton_sbackground_sfocus = srgb(0.125, 0.25, 0.375, 0.5)

print(
  "{\"language\":\"swift\",\"contexts\":{" + contexts.joined(separator: ",") + "},"
    + "\"scenarios\":{\"semantic\":\(dump(DesignTokens.Tokens(overrides: semantic))),"
    + "\"component\":\(dump(DesignTokens.Tokens(overrides: component)))}}")
