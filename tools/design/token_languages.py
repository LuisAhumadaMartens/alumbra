import json

from token_model import KOTLIN_TYPES, SWIFT_TYPES, TS_TYPES, camel, fail, literal


def override_chain(catalogue, token):
    chain = [token]
    for item in catalogue.deprecations:
        if item.replacement == token.name:
            chain.extend(override_chain(catalogue, item))
    return chain


def color_arguments(value):
    arguments = [literal(part) for part in value["components"]]
    if value["colorSpace"] == "srgb":
        return arguments + ([] if value["alpha"] == 1 else [literal(value["alpha"])])
    return arguments + [literal(value["alpha"])] + [literal(part) for part in value["srgb"]]


def script_value(kind, value, language):
    if kind == "color":
        if value["colorSpace"] == "srgb":
            return f"srgb({', '.join(color_arguments(value))})"
        space = {
            "ts": json.dumps(value["colorSpace"]),
            "swift": "." + camel(value["colorSpace"], False),
            "kotlin": "ColorSpace." + value["colorSpace"].replace("-", "_").upper(),
        }[language]
        return f"wide({space}, {', '.join(color_arguments(value))})"
    if kind in {"dimension", "duration", "number", "fontWeight"}:
        return literal(value)
    if kind == "fontFamily":
        names = ", ".join(json.dumps(name) for name in value)
        return f"listOf({names})" if language == "kotlin" else f"[{names}]"
    if kind == "cubicBezier":
        return f"bezier({', '.join(literal(part) for part in value)})"
    if kind == "shadow":
        layers = []
        for layer in value:
            arguments = [script_value("color", layer["color"], language)]
            arguments += [literal(layer[key]) for key in ("offsetX", "offsetY", "blur", "spread")]
            arguments.append("true" if layer["inset"] else "false")
            layers.append(f"layer({', '.join(arguments)})")
        return f"listOf({', '.join(layers)})" if language == "kotlin" else f"[{', '.join(layers)}]"
    fail(f"token type {kind!r} has no generated representation")


def table_lines(token, language, indent):
    pad = " " * indent
    values = [script_value(token.kind, value, language) for value in token.table]
    opening = "listOf(" if language == "kotlin" else "["
    closing = ")" if language == "kotlin" else "]"
    return [opening, *[f"{pad}  {value}," for value in values], f"{pad}{closing}"]


def helpers_used(catalogue):
    used = {"select"}
    for token in catalogue.public:
        if token.alias is not None:
            continue
        for value in token.table:
            colors = []
            if token.kind == "color":
                colors.append(value)
            elif token.kind == "shadow":
                used.add("layer")
                colors.extend(layer["color"] for layer in value)
            elif token.kind == "cubicBezier":
                used.add("bezier")
            for color in colors:
                used.update({"rgb", "srgb" if color["colorSpace"] == "srgb" else "wide"})
    return used


def helper_lines(blocks, used):
    lines = []
    for name, block in blocks:
        if name in used:
            lines += (["", *block] if lines else block)
    return lines


def fallback_expression(catalogue, token, language):
    if token.alias is not None:
        return catalogue.by_name[token.alias].identifier
    if language == "ts":
        modifiers = ", ".join(json.dumps(modifier.field) for modifier in token.modifiers)
        return f"select(defaults.{token.identifier}, context, [{modifiers}])"
    if language == "swift":
        modifiers = ", ".join("." + modifier.field for modifier in token.modifiers)
        return f"select(Defaults.{token.identifier}, context, [{modifiers}])"
    modifiers = "".join(", Modifier." + modifier.snake.upper() for modifier in token.modifiers)
    return f"select(Defaults.{token.identifier}, context{modifiers})"


def render_ts(catalogue):
    modifiers = catalogue.modifiers
    lines = []
    for modifier in modifiers:
        options = " | ".join(json.dumps(context) for context in modifier.contexts)
        lines.append(f"export type {modifier.type_name} = {options};")
    lines += ["", "export interface Context {"]
    lines += [f"  readonly {modifier.field}: {modifier.type_name};" for modifier in modifiers]
    lines += ["}", "", "export const contexts = {"]
    for modifier in modifiers:
        options = ", ".join(json.dumps(context) for context in modifier.contexts)
        lines.append(f"  {modifier.field}: [{options}],")
    lines += ["} as const;", "", "export const defaultContext: Context = {"]
    lines += [f"  {modifier.field}: {json.dumps(modifier.default)}," for modifier in modifiers]
    lines += ["};", ""]
    lines += TS_TYPES_BLOCK
    lines += ["", "export interface Tokens {"]
    lines += [
        f"  readonly {token.identifier}: {TS_TYPES[token.kind]};" for token in catalogue.public
    ]
    lines += ["}", "", "export type Overrides = Partial<Tokens>;", ""]
    for name, attribute in (("tokenNames", "name"), ("cssProperties", "css")):
        lines.append(f"export const {name}: {{ readonly [K in keyof Tokens]: string }} = {{")
        lines += [
            f"  {token.identifier}: {json.dumps(getattr(token, attribute))},"
            for token in catalogue.public
        ]
        lines += ["};", ""]
    lines.append("export const tokenTypes: { readonly [K in keyof Tokens]: TokenType } = {")
    lines += [f"  {token.identifier}: {json.dumps(token.kind)}," for token in catalogue.public]
    lines += ["};", ""]
    lines.append(
        "export const tokenAliases: { readonly [K in keyof Tokens]?: keyof Tokens } = {")
    lines += [
        f"  {token.identifier}: {json.dumps(catalogue.by_name[token.alias].identifier)},"
        for token in catalogue.public if token.alias is not None
    ]
    lines += ["};", ""]
    lines += helper_lines(TS_HELPERS, helpers_used(catalogue))
    lines += ["", "const defaults = {"]
    for token in catalogue.public:
        if token.alias is None:
            table = table_lines(token, "ts", 2)
            lines.append(f"  {token.identifier}: {table[0]}")
            lines += table[1:-1]
            lines.append(table[-1] + ",")
    lines += ["} as const;", ""]
    lines += [
        "export function resolveTokens(",
        "  context: Context = defaultContext,",
        "  overrides: Overrides = {},",
        "): Tokens {",
    ]
    for token in catalogue.public:
        name = token.identifier
        if token.replacement is not None:
            lines.append(f"  const {name} = {catalogue.by_name[token.alias].identifier};")
            continue
        chain = " ?? ".join(
            f"overrides.{item.identifier}" for item in override_chain(catalogue, token))
        lines += [
            f"  const {name} =",
            f"    {chain} ??",
            f"    {fallback_expression(catalogue, token, 'ts')};",
        ]
    lines.append("  return {")
    lines += [f"    {token.identifier}," for token in catalogue.public]
    lines += ["  };", "}", ""]
    return "\n".join(lines)


TS_TYPES_BLOCK = [
    'export type ColorSpace = "srgb" | "display-p3" | "rec2020";',
    "",
    "export type TokenType =",
    '  | "color"',
    '  | "dimension"',
    '  | "duration"',
    '  | "number"',
    '  | "fontWeight"',
    '  | "fontFamily"',
    '  | "cubicBezier"',
    '  | "shadow";',
    "",
    "export interface Rgb {",
    "  readonly red: number;",
    "  readonly green: number;",
    "  readonly blue: number;",
    "}",
    "",
    "export interface Color {",
    "  readonly colorSpace: ColorSpace;",
    "  readonly components: Rgb;",
    "  readonly alpha: number;",
    "  readonly srgb: Rgb;",
    "}",
    "",
    "export interface CubicBezier {",
    "  readonly x1: number;",
    "  readonly y1: number;",
    "  readonly x2: number;",
    "  readonly y2: number;",
    "}",
    "",
    "export interface ShadowLayer {",
    "  readonly color: Color;",
    "  readonly offsetX: number;",
    "  readonly offsetY: number;",
    "  readonly blur: number;",
    "  readonly spread: number;",
    "  readonly inset: boolean;",
    "}",
    "",
    "export type FontFamily = readonly string[];",
    "",
    "export type Shadow = readonly ShadowLayer[];",
]

TS_HELPERS = [
    ("rgb", [
        "function rgb(red: number, green: number, blue: number): Rgb {",
        "  return { red, green, blue };",
        "}",
    ]),
    ("srgb", [
        "function srgb(red: number, green: number, blue: number, alpha = 1): Color {",
        "  const components = rgb(red, green, blue);",
        '  return { colorSpace: "srgb", components, alpha, srgb: components };',
        "}",
    ]),
    ("wide", [
        "function wide(",
        "  colorSpace: ColorSpace,",
        "  red: number,",
        "  green: number,",
        "  blue: number,",
        "  alpha: number,",
        "  srgbRed: number,",
        "  srgbGreen: number,",
        "  srgbBlue: number,",
        "): Color {",
        "  return {",
        "    colorSpace,",
        "    components: rgb(red, green, blue),",
        "    alpha,",
        "    srgb: rgb(srgbRed, srgbGreen, srgbBlue),",
        "  };",
        "}",
    ]),
    ("bezier", [
        "function bezier(x1: number, y1: number, x2: number, y2: number): CubicBezier {",
        "  return { x1, y1, x2, y2 };",
        "}",
    ]),
    ("layer", [
        "function layer(",
        "  color: Color,",
        "  offsetX: number,",
        "  offsetY: number,",
        "  blur: number,",
        "  spread: number,",
        "  inset: boolean,",
        "): ShadowLayer {",
        "  return { color, offsetX, offsetY, blur, spread, inset };",
        "}",
    ]),
    ("select", [
        "function select<T>(",
        "  values: readonly T[],",
        "  context: Context,",
        "  modifiers: readonly (keyof Context)[],",
        "): T {",
        "  let index = 0;",
        "  for (const modifier of modifiers) {",
        "    const options: readonly string[] = contexts[modifier];",
        "    const position = options.indexOf(context[modifier]);",
        "    if (position < 0) {",
        "      throw new RangeError(`Unknown ${modifier} context: ${context[modifier]}`);",
        "    }",
        "    index = index * options.length + position;",
        "  }",
        "  const value = values[index];",
        "  if (value === undefined) {",
        '    throw new RangeError("Token table does not cover its contexts");',
        "  }",
        "  return value;",
        "}",
    ]),
]


def render_swift(catalogue):
    modifiers = catalogue.modifiers
    lines = [
        "#if canImport(SwiftUI)", "  import SwiftUI", "#endif", "", "public enum DesignTokens {",
    ]
    for modifier in modifiers:
        lines.append(f"  public enum {modifier.type_name}: String, CaseIterable, Sendable {{")
        for context in modifier.contexts:
            case = camel(context, False)
            suffix = "" if case == context else f" = {json.dumps(context)}"
            lines.append(f"    case {case}{suffix}")
        lines += ["", "    var position: Int {", "      switch self {"]
        for index, context in enumerate(modifier.contexts):
            lines.append(f"      case .{camel(context, False)}: {index}")
        lines += ["      }", "    }", "  }", ""]
    lines += ["  public struct Context: Equatable, Sendable {"]
    lines += [f"    public var {modifier.field}: {modifier.type_name}" for modifier in modifiers]
    lines += ["", "    public init("]
    lines += [
        f"      {modifier.field}: {modifier.type_name} = .{camel(modifier.default, False)}"
        + ("," if index < len(modifiers) - 1 else "")
        for index, modifier in enumerate(modifiers)
    ]
    lines.append("    ) {")
    lines += [f"      self.{modifier.field} = {modifier.field}" for modifier in modifiers]
    lines += ["    }", "  }", ""]
    lines += SWIFT_TYPES_BLOCK
    lines += ["", "  public struct Overrides: Sendable {"]
    lines += [
        f"    public var {token.identifier}: {SWIFT_TYPES[token.kind]}?"
        for token in catalogue.public
    ]
    lines += ["", "    public init() {}", "  }", "", "  public struct Tokens: Sendable {"]
    lines += [
        f"    public let {token.identifier}: {SWIFT_TYPES[token.kind]}"
        for token in catalogue.public
    ]
    lines += [
        "",
        "    public init(context: Context = Context(), overrides: Overrides = Overrides()) {",
    ]
    for token in catalogue.public:
        name = token.identifier
        if token.replacement is not None:
            lines.append(f"      {name} = {catalogue.by_name[token.alias].identifier}")
            continue
        chain = " ?? ".join(
            f"overrides.{item.identifier}" for item in override_chain(catalogue, token))
        lines += [
            f"      {name} =",
            f"        {chain}",
            f"        ?? {fallback_expression(catalogue, token, 'swift')}",
        ]
    lines += ["    }", "", "    public var entries: [(name: String, value: Value)] {", "      ["]
    lines += [
        f"        ({json.dumps(token.name)}, .{token.kind}({token.identifier})),"
        for token in catalogue.public
    ]
    lines += ["      ]", "    }", "  }", ""]
    lines += SWIFT_HELPERS
    lines += ["", "  private enum Defaults {"]
    for token in catalogue.public:
        if token.alias is None:
            table = table_lines(token, "swift", 4)
            lines.append(
                f"    static let {token.identifier}: [{SWIFT_TYPES[token.kind]}] = {table[0]}")
            lines += ["  " + line for line in table[1:]]
    lines += ["  }", "}", ""]
    lines += SWIFT_UI_BLOCK
    return "\n".join(lines)


SWIFT_TYPES_BLOCK = [
    "  public enum ColorSpace: String, Sendable {",
    "    case srgb",
    '    case displayP3 = "display-p3"',
    "    case rec2020",
    "  }",
    "",
    "  public struct Rgb: Equatable, Sendable {",
    "    public let red: Double",
    "    public let green: Double",
    "    public let blue: Double",
    "",
    "    public init(red: Double, green: Double, blue: Double) {",
    "      self.red = red",
    "      self.green = green",
    "      self.blue = blue",
    "    }",
    "  }",
    "",
    "  public struct Color: Equatable, Sendable {",
    "    public let colorSpace: ColorSpace",
    "    public let components: Rgb",
    "    public let alpha: Double",
    "    public let srgb: Rgb",
    "",
    "    public init(colorSpace: ColorSpace, components: Rgb, alpha: Double, srgb: Rgb) {",
    "      self.colorSpace = colorSpace",
    "      self.components = components",
    "      self.alpha = alpha",
    "      self.srgb = srgb",
    "    }",
    "  }",
    "",
    "  public struct CubicBezier: Equatable, Sendable {",
    "    public let x1: Double",
    "    public let y1: Double",
    "    public let x2: Double",
    "    public let y2: Double",
    "",
    "    public init(x1: Double, y1: Double, x2: Double, y2: Double) {",
    "      self.x1 = x1",
    "      self.y1 = y1",
    "      self.x2 = x2",
    "      self.y2 = y2",
    "    }",
    "  }",
    "",
    "  public struct ShadowLayer: Equatable, Sendable {",
    "    public let color: Color",
    "    public let offsetX: Double",
    "    public let offsetY: Double",
    "    public let blur: Double",
    "    public let spread: Double",
    "    public let inset: Bool",
    "",
    "    public init(",
    "      color: Color, offsetX: Double, offsetY: Double, blur: Double, spread: Double, inset: Bool",
    "    ) {",
    "      self.color = color",
    "      self.offsetX = offsetX",
    "      self.offsetY = offsetY",
    "      self.blur = blur",
    "      self.spread = spread",
    "      self.inset = inset",
    "    }",
    "  }",
    "",
    "  public enum Value: Equatable, Sendable {",
    "    case color(Color)",
    "    case dimension(Double)",
    "    case duration(Double)",
    "    case number(Double)",
    "    case fontWeight(Double)",
    "    case fontFamily([String])",
    "    case cubicBezier(CubicBezier)",
    "    case shadow([ShadowLayer])",
    "  }",
]

SWIFT_HELPERS = [
    "  enum Modifier {",
    "    case theme",
    "    case contrast",
    "    case density",
    "    case motion",
    "    case dynamicRange",
    "  }",
    "",
    "  static func select<T>(_ values: [T], _ context: Context, _ modifiers: [Modifier]) -> T {",
    "    var index = 0",
    "    for modifier in modifiers {",
    "      switch modifier {",
    "      case .theme:",
    "        index = index * Theme.allCases.count + context.theme.position",
    "      case .contrast:",
    "        index = index * Contrast.allCases.count + context.contrast.position",
    "      case .density:",
    "        index = index * Density.allCases.count + context.density.position",
    "      case .motion:",
    "        index = index * Motion.allCases.count + context.motion.position",
    "      case .dynamicRange:",
    "        index = index * DynamicRange.allCases.count + context.dynamicRange.position",
    "      }",
    "    }",
    "    return values[index]",
    "  }",
    "",
    "  static func srgb(_ red: Double, _ green: Double, _ blue: Double, _ alpha: Double = 1)",
    "    -> Color",
    "  {",
    "    let components = Rgb(red: red, green: green, blue: blue)",
    "    return Color(colorSpace: .srgb, components: components, alpha: alpha, srgb: components)",
    "  }",
    "",
    "  static func wide(",
    "    _ colorSpace: ColorSpace, _ red: Double, _ green: Double, _ blue: Double,",
    "    _ alpha: Double, _ srgbRed: Double, _ srgbGreen: Double, _ srgbBlue: Double",
    "  ) -> Color {",
    "    Color(",
    "      colorSpace: colorSpace, components: Rgb(red: red, green: green, blue: blue),",
    "      alpha: alpha, srgb: Rgb(red: srgbRed, green: srgbGreen, blue: srgbBlue))",
    "  }",
    "",
    "  static func bezier(_ x1: Double, _ y1: Double, _ x2: Double, _ y2: Double)",
    "    -> CubicBezier",
    "  {",
    "    CubicBezier(x1: x1, y1: y1, x2: x2, y2: y2)",
    "  }",
    "",
    "  static func layer(",
    "    _ color: Color, _ offsetX: Double, _ offsetY: Double, _ blur: Double, _ spread: Double,",
    "    _ inset: Bool",
    "  ) -> ShadowLayer {",
    "    ShadowLayer(",
    "      color: color, offsetX: offsetX, offsetY: offsetY, blur: blur, spread: spread,",
    "      inset: inset)",
    "  }",
]

SWIFT_UI_BLOCK = [
    "#if canImport(SwiftUI)",
    "  extension DesignTokens.Color {",
    "    public var swiftUIColor: SwiftUI.Color {",
    "      switch colorSpace {",
    "      case .srgb:",
    "        SwiftUI.Color(",
    "          .sRGB, red: components.red, green: components.green, blue: components.blue,",
    "          opacity: alpha)",
    "      case .displayP3:",
    "        SwiftUI.Color(",
    "          .displayP3, red: components.red, green: components.green, blue: components.blue,",
    "          opacity: alpha)",
    "      case .rec2020:",
    "        SwiftUI.Color(.sRGB, red: srgb.red, green: srgb.green, blue: srgb.blue, opacity: alpha)",
    "      }",
    "    }",
    "  }",
    "#endif",
    "",
]


def render_kotlin(catalogue):
    modifiers = catalogue.modifiers
    lines = ["package org.alumbra.designtokens", ""]
    for modifier in modifiers:
        lines.append(f"enum class {modifier.type_name}(val id: String) {{")
        lines += [
            f"    {context.replace('-', '_').upper()}({json.dumps(context)}),"
            for context in modifier.contexts
        ]
        lines += ["}", ""]
    lines.append("data class Context(")
    lines += [
        f"    val {modifier.field}: {modifier.type_name} = "
        f"{modifier.type_name}.{modifier.default.replace('-', '_').upper()},"
        for modifier in modifiers
    ]
    lines += [")", ""]
    lines += KOTLIN_TYPES_BLOCK
    lines += ["", "class Overrides {"]
    lines += [
        f"    var {token.identifier}: {KOTLIN_TYPES[token.kind]}? = null"
        for token in catalogue.public
    ]
    lines += [
        "}",
        "",
        "class Tokens(context: Context = Context(), overrides: Overrides = Overrides()) {",
    ]
    for token in catalogue.public:
        name = token.identifier
        kind = KOTLIN_TYPES[token.kind]
        if token.replacement is not None:
            lines.append(f"    val {name}: {kind} = {catalogue.by_name[token.alias].identifier}")
            continue
        chain = " ?: ".join(
            f"overrides.{item.identifier}" for item in override_chain(catalogue, token))
        lines += [
            f"    val {name}: {kind} =",
            f"        {chain}",
            f"            ?: {fallback_expression(catalogue, token, 'kotlin')}",
        ]
    lines += ["", "    val entries: List<Pair<String, Value>>", "        get() = listOf("]
    lines += [
        f"            {json.dumps(token.name)} to "
        f"Value.{camel(token.kind, True)}Value({token.identifier}),"
        for token in catalogue.public
    ]
    lines += ["        )", "}", ""]
    lines += helper_lines(KOTLIN_HELPERS, helpers_used(catalogue) | {"modifier"})
    lines += ["", "private object Defaults {"]
    for token in catalogue.public:
        if token.alias is None:
            table = table_lines(token, "kotlin", 4)
            lines.append(
                f"    val {token.identifier}: List<{KOTLIN_TYPES[token.kind]}> = {table[0]}")
            lines += table[1:]
    lines += ["}", ""]
    return "\n".join(lines)


KOTLIN_TYPES_BLOCK = [
    "enum class ColorSpace(val id: String) {",
    '    SRGB("srgb"),',
    '    DISPLAY_P3("display-p3"),',
    '    REC2020("rec2020"),',
    "}",
    "",
    "data class Rgb(val red: Double, val green: Double, val blue: Double)",
    "",
    "data class Color(",
    "    val colorSpace: ColorSpace,",
    "    val components: Rgb,",
    "    val alpha: Double,",
    "    val srgb: Rgb,",
    ")",
    "",
    "data class CubicBezier(val x1: Double, val y1: Double, val x2: Double, val y2: Double)",
    "",
    "data class ShadowLayer(",
    "    val color: Color,",
    "    val offsetX: Double,",
    "    val offsetY: Double,",
    "    val blur: Double,",
    "    val spread: Double,",
    "    val inset: Boolean,",
    ")",
    "",
    "sealed interface Value {",
    "    data class ColorValue(val value: Color) : Value",
    "",
    "    data class DimensionValue(val value: Double) : Value",
    "",
    "    data class DurationValue(val value: Double) : Value",
    "",
    "    data class NumberValue(val value: Double) : Value",
    "",
    "    data class FontWeightValue(val value: Double) : Value",
    "",
    "    data class FontFamilyValue(val value: List<String>) : Value",
    "",
    "    data class CubicBezierValue(val value: CubicBezier) : Value",
    "",
    "    data class ShadowValue(val value: List<ShadowLayer>) : Value",
    "}",
]

KOTLIN_HELPERS = [
    ("modifier", [
        "private enum class Modifier {",
        "    THEME,",
        "    CONTRAST,",
        "    DENSITY,",
        "    MOTION,",
        "    DYNAMIC_RANGE,",
        "}",
    ]),
    ("select", [
        "private fun <T> select(values: List<T>, context: Context, vararg modifiers: Modifier): T {",
        "    var index = 0",
        "    for (modifier in modifiers) {",
        "        index = when (modifier) {",
        "            Modifier.THEME -> index * Theme.entries.size + context.theme.ordinal",
        "            Modifier.CONTRAST -> index * Contrast.entries.size + context.contrast.ordinal",
        "            Modifier.DENSITY -> index * Density.entries.size + context.density.ordinal",
        "            Modifier.MOTION -> index * Motion.entries.size + context.motion.ordinal",
        "            Modifier.DYNAMIC_RANGE ->",
        "                index * DynamicRange.entries.size + context.dynamicRange.ordinal",
        "        }",
        "    }",
        "    return values[index]",
        "}",
    ]),
    ("srgb", [
        "private fun srgb(red: Double, green: Double, blue: Double, alpha: Double = 1.0): Color {",
        "    val components = Rgb(red, green, blue)",
        "    return Color(ColorSpace.SRGB, components, alpha, components)",
        "}",
    ]),
    ("wide", [
        "private fun wide(",
        "    colorSpace: ColorSpace,",
        "    red: Double,",
        "    green: Double,",
        "    blue: Double,",
        "    alpha: Double,",
        "    srgbRed: Double,",
        "    srgbGreen: Double,",
        "    srgbBlue: Double,",
        "): Color = Color(colorSpace, Rgb(red, green, blue), alpha, Rgb(srgbRed, srgbGreen, srgbBlue))",
    ]),
    ("bezier", [
        "private fun bezier(x1: Double, y1: Double, x2: Double, y2: Double): CubicBezier =",
        "    CubicBezier(x1, y1, x2, y2)",
    ]),
    ("layer", [
        "private fun layer(",
        "    color: Color,",
        "    offsetX: Double,",
        "    offsetY: Double,",
        "    blur: Double,",
        "    spread: Double,",
        "    inset: Boolean,",
        "): ShadowLayer = ShadowLayer(color, offsetX, offsetY, blur, spread, inset)",
    ]),
]
