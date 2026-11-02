import json

from token_model import COLUMNS, CPP_TYPES, camel, fail, literal

GROUPS = {
    "Color": ("Color", "Color"),
    "Pixels": ("double", "Number"),
    "double": ("double", "Number"),
    "Milliseconds": ("Milliseconds", "Duration"),
    "FontFamily": ("FontFamily", "FontFamily"),
    "CubicBezier": ("CubicBezier", "CubicBezier"),
    "Shadow": ("Shadow", "Shadow"),
}
GROUP_ORDER = ("Color", "Number", "Duration", "FontFamily", "CubicBezier", "Shadow")
COLOR_SPACES = {"srgb": "kSrgb", "display-p3": "kDisplayP3", "rec2020": "kRec2020"}


def fits(line):
    return len(line) <= COLUMNS


def designated(indent, field, value):
    pad = " " * indent
    line = f"{pad}.{field} = {value},"
    if fits(line):
        return [line]
    return [f"{pad}.{field} =", f"{pad}    {value},"]


def declaration(indent, kind, name, initializer=""):
    pad = " " * indent
    line = f"{pad}{kind} {name}{initializer};"
    if fits(line):
        return [line]
    return [f"{pad}{kind}", f"{pad}    {name}{initializer};"]


class Registry:
    def __init__(self):
        self.colors = {}
        self.families = {}
        self.shadows = {}

    @staticmethod
    def register(table, prefix, value):
        key = json.dumps(value, sort_keys=True)
        if key not in table:
            table[key] = (f"{prefix}{len(table)}", value)
        return table[key][0]

    def color(self, value):
        if value["colorSpace"] == "srgb":
            parts = [literal(part) for part in value["components"]]
            if value["alpha"] == 1:
                return f"Srgb({', '.join(parts)})"
            return f"Srgba({', '.join(parts + [literal(value['alpha'])])})"
        return self.register(self.colors, "kWideColor", value)

    def value(self, kind, value):
        if kind == "color":
            return self.color(value)
        if kind in {"dimension", "number", "fontWeight"}:
            return literal(value)
        if kind == "duration":
            return f"Milliseconds({literal(value)})"
        if kind == "fontFamily":
            return f"FontFamily({self.register(self.families, 'kFontFamily', value)})"
        if kind == "cubicBezier":
            return f"Bezier({', '.join(literal(part) for part in value)})"
        if kind == "shadow":
            for layer in value:
                self.color(layer["color"])
            return f"Shadow({self.register(self.shadows, 'kShadow', value)})"
        fail(f"token type {kind!r} has no C++ representation")

    def definitions(self):
        lines = []
        for name, value in self.colors.values():
            lines.append(f"inline constexpr Color {name} = {{")
            lines += designated(4, "color_space", f"ColorSpace::{COLOR_SPACES[value['colorSpace']]}")
            lines += designated(4, "components", rgb(value["components"]))
            lines += designated(4, "alpha", literal(value["alpha"]))
            lines += designated(4, "srgb", rgb(value["srgb"]))
            lines += ["};", ""]
        for name, value in self.families.values():
            lines.append(
                f"inline constexpr std::array<std::string_view, {len(value)}> {name} = {{{{")
            lines += [f"    {json.dumps(item)}," for item in value]
            lines += ["}};", ""]
        for name, value in self.shadows.values():
            lines.append(f"inline constexpr std::array<ShadowLayer, {len(value)}> {name} = {{{{")
            for layer in value:
                lines.append("    {")
                lines += designated(8, "color", self.color(layer["color"]))
                for field, key in (
                        ("offset_x", "offsetX"), ("offset_y", "offsetY"), ("blur", "blur"),
                        ("spread", "spread")):
                    lines += designated(8, field, literal(layer[key]))
                lines += designated(8, "inset", "true" if layer["inset"] else "false")
                lines.append("    },")
            lines += ["}};", ""]
        return lines


def rgb(components):
    return f"MakeRgb({', '.join(literal(part) for part in components)})"


def group(token):
    return GROUPS[CPP_TYPES[token.kind]]


def member_initializer(token):
    kind = CPP_TYPES[token.kind]
    if kind in {"Pixels", "double"}:
        return " = 0.0"
    if kind == "Milliseconds":
        return "{}"
    return ""


def modifier_mask(token):
    if not token.modifiers:
        return "0U"
    return " | ".join(f"k{modifier.type_name}" for modifier in token.modifiers)


def render_cpp(catalogue):
    modifiers = catalogue.modifiers
    public = catalogue.public
    lines = [
        "#ifndef ALUMBRA_DESIGN_TOKENS_DESIGN_TOKENS_H_",
        "#define ALUMBRA_DESIGN_TOKENS_DESIGN_TOKENS_H_",
        "",
        "#include <array>",
        "#include <chrono>",
        "#include <cstddef>",
        "#include <optional>",
        "#include <ratio>",
        "#include <span>",
        "#include <string_view>",
        "",
        "namespace alumbra::design_tokens {",
        "",
    ]
    for modifier in modifiers:
        values = ", ".join("k" + camel(context, True) for context in modifier.contexts)
        lines.append(f"enum class {modifier.type_name} {{ {values} }};")
    lines += ["", "struct Context {"]
    for modifier in modifiers:
        default = "k" + camel(modifier.default, True)
        lines.append(f"  {modifier.type_name} {modifier.snake} = {modifier.type_name}::{default};")
    lines += ["};", ""]
    for modifier in modifiers:
        values = [f"{modifier.type_name}::k{camel(context, True)}" for context in modifier.contexts]
        head = (
            f"inline constexpr std::array<{modifier.type_name}, {len(values)}> "
            f"k{modifier.type_name}Values = {{{{")
        if fits(f"{head}{', '.join(values)}}}}};"):
            lines.append(f"{head}{', '.join(values)}}}}};")
        else:
            lines += [head, *[f"    {value}," for value in values], "}};"]
    for modifier in modifiers:
        lines += [
            "",
            f"[[nodiscard]] constexpr std::string_view Name({modifier.type_name} value) {{",
            "  switch (value) {",
        ]
        for context in modifier.contexts:
            lines += [
                f"    case {modifier.type_name}::k{camel(context, True)}:",
                f"      return {json.dumps(context)};",
            ]
        lines += ["  }", "  return {};", "}"]
    lines.append("")
    lines += CPP_TYPES_BLOCK
    lines += ["", "struct Tokens {"]
    for token in public:
        lines += declaration(
            2, CPP_TYPES[token.kind], token.identifier, member_initializer(token))
    lines += ["};", "", "struct Overrides {"]
    for token in public:
        lines += declaration(2, f"std::optional<{CPP_TYPES[token.kind]}>", token.identifier)
    lines += ["};", "", "namespace internal {", ""]
    for index, modifier in enumerate(modifiers):
        lines.append(f"inline constexpr unsigned k{modifier.type_name} = {1 << index}U;")
    lines.append("")
    lines += CPP_HELPERS_BLOCK
    lines += [
        "",
        "[[nodiscard]] constexpr std::size_t Index(const Context& context,",
        "                                          unsigned modifiers) {",
        "  std::size_t index = 0;",
    ]
    for modifier in modifiers:
        scale = f"    index = (index * {len(modifier.contexts)}) +"
        offset = f"static_cast<std::size_t>(context.{modifier.snake});"
        lines.append(f"  if ((modifiers & k{modifier.type_name}) != 0U) {{")
        if fits(f"{scale} {offset}"):
            lines.append(f"{scale} {offset}")
        else:
            lines += [scale, f"            {offset}"]
        lines.append("  }")
    lines += ["  return index;", "}", ""]
    lines += CPP_GENERIC_BLOCK
    lines.append("")
    registry = Registry()
    defaults = []
    for token in public:
        if token.alias is None:
            defaults.append((token, [registry.value(token.kind, value) for value in token.table]))
    lines += registry.definitions()
    lines.append("struct Defaults {")
    for token, values in defaults:
        trivial = CPP_TYPES[token.kind] in {"Pixels", "double", "Milliseconds"}
        lines += declaration(
            2, f"std::array<{CPP_TYPES[token.kind]}, {len(values)}>", token.identifier,
            "{}" if trivial else "")
    lines += ["};", "", "inline constexpr Defaults kDefaults = {"]
    for token, values in defaults:
        lines.append(f"    .{token.identifier} = {{{{")
        lines += [f"        {value}," for value in values]
        lines.append("    }},")
    lines += ["};", ""]
    tables = {name: ([], []) for name in GROUP_ORDER}
    for token in public:
        tables[group(token)[1]][0 if token.alias is None else 1].append(token)
    deprecations = {name: [] for name in GROUP_ORDER}
    for token in catalogue.deprecations:
        deprecations[group(token)[1]].append(token)
    for name in GROUP_ORDER:
        values, aliases = tables[name]
        kind = next((group(token)[0] for token in values + aliases), None)
        if values:
            lines.append(
                f"inline constexpr ValueTable<{kind}, {len(values)}> k{name}Values = {{{{")
            for token in values:
                lines.append("    {")
                lines += designated(8, "name", json.dumps(token.name))
                lines += designated(8, "token", f"&Tokens::{token.identifier}")
                lines += designated(8, "value", f"&Overrides::{token.identifier}")
                lines += designated(8, "defaults", f"kDefaults.{token.identifier}")
                lines += designated(8, "modifiers", modifier_mask(token))
                lines.append("    },")
            lines += ["}};", ""]
        if aliases:
            lines.append(
                f"inline constexpr AliasTable<{kind}, {len(aliases)}> k{name}Aliases = {{{{")
            for token in aliases:
                target = catalogue.by_name[token.alias].identifier
                lines.append("    {")
                lines += designated(8, "name", json.dumps(token.name))
                lines += designated(8, "token", f"&Tokens::{token.identifier}")
                value = "nullptr" if token.replacement else f"&Overrides::{token.identifier}"
                lines += designated(8, "value", value)
                lines += designated(8, "target", f"&Tokens::{target}")
                lines.append("    },")
            lines += ["}};", ""]
        if deprecations[name]:
            lines.append(
                f"inline constexpr DeprecationTable<{kind}, {len(deprecations[name])}> "
                f"k{name}Deprecations = {{{{")
            for token in deprecations[name]:
                replacement = catalogue.by_name[token.replacement].identifier
                lines.append("    {")
                lines += designated(8, "replacement", f"&Overrides::{replacement}")
                lines += designated(8, "deprecated", f"&Overrides::{token.identifier}")
                lines.append("    },")
            lines += ["}};", ""]
    has_deprecations = any(deprecations.values())
    lines += [
        "[[nodiscard]] constexpr Tokens ResolveAll(const Context& context,",
        "                                          const Overrides& overrides) {",
    ]
    effective = "overrides"
    if has_deprecations:
        effective = "effective"
        lines.append("  Overrides effective = overrides;")
        for name in GROUP_ORDER:
            if deprecations[name]:
                lines.append(f"  ApplyDeprecations(k{name}Deprecations, effective);")
    lines.append("  Tokens tokens;")
    for name in GROUP_ORDER:
        if tables[name][0]:
            lines.append(f"  ResolveValues(k{name}Values, context, {effective}, tokens);")
    for name in GROUP_ORDER:
        if tables[name][1]:
            lines.append(f"  ResolveAliases(k{name}Aliases, {effective}, tokens);")
    lines += [
        "  return tokens;",
        "}",
        "",
        "template <typename Visitor>",
        "constexpr void VisitAll(const Tokens& tokens, Visitor& visitor) {",
    ]
    for name in GROUP_ORDER:
        for index, suffix in enumerate(("Values", "Aliases")):
            if tables[name][index]:
                lines.append(f"  VisitEntries(k{name}{suffix}, tokens, visitor);")
    lines += ["}", "", "}", ""]
    lines += CPP_API_BLOCK
    return "\n".join(lines)


CPP_TYPES_BLOCK = [
    "enum class ColorSpace { kSrgb, kDisplayP3, kRec2020 };",
    "",
    "struct Rgb {",
    "  double red = 0.0;",
    "  double green = 0.0;",
    "  double blue = 0.0;",
    "};",
    "",
    "struct Color {",
    "  ColorSpace color_space = ColorSpace::kSrgb;",
    "  Rgb components;",
    "  double alpha = 1.0;",
    "  Rgb srgb;",
    "};",
    "",
    "struct CubicBezier {",
    "  double x1 = 0.0;",
    "  double y1 = 0.0;",
    "  double x2 = 1.0;",
    "  double y2 = 1.0;",
    "};",
    "",
    "struct ShadowLayer {",
    "  Color color;",
    "  double offset_x = 0.0;",
    "  double offset_y = 0.0;",
    "  double blur = 0.0;",
    "  double spread = 0.0;",
    "  bool inset = false;",
    "};",
    "",
    "using Pixels = double;",
    "using Milliseconds = std::chrono::duration<double, std::milli>;",
    "using FontFamily = std::span<const std::string_view>;",
    "using Shadow = std::span<const ShadowLayer>;",
]

CPP_HELPERS_BLOCK = [
    "[[nodiscard]] constexpr Rgb MakeRgb(double red, double green, double blue) {",
    "  return {.red = red, .green = green, .blue = blue};",
    "}",
    "",
    "[[nodiscard]] constexpr Color Srgba(double red, double green, double blue,",
    "                                    double alpha) {",
    "  const Rgb components = MakeRgb(red, green, blue);",
    "  return {.color_space = ColorSpace::kSrgb,",
    "          .components = components,",
    "          .alpha = alpha,",
    "          .srgb = components};",
    "}",
    "",
    "[[nodiscard]] constexpr Color Srgb(double red, double green, double blue) {",
    "  return Srgba(red, green, blue, 1.0);",
    "}",
    "",
    "[[nodiscard]] constexpr CubicBezier Bezier(double x1, double y1, double x2,",
    "                                           double y2) {",
    "  return {.x1 = x1, .y1 = y1, .x2 = x2, .y2 = y2};",
    "}",
    "",
    "template <typename T>",
    "struct ValueEntry {",
    "  std::string_view name;",
    "  T Tokens::* token;",
    "  std::optional<T> Overrides::* value;",
    "  std::span<const T> defaults;",
    "  unsigned modifiers = 0U;",
    "};",
    "",
    "template <typename T>",
    "struct AliasEntry {",
    "  std::string_view name;",
    "  T Tokens::* token;",
    "  std::optional<T> Overrides::* value;",
    "  T Tokens::* target;",
    "};",
    "",
    "template <typename T>",
    "struct Deprecation {",
    "  std::optional<T> Overrides::* replacement;",
    "  std::optional<T> Overrides::* deprecated;",
    "};",
    "",
    "template <typename T, std::size_t N>",
    "using ValueTable = std::array<ValueEntry<T>, N>;",
    "",
    "template <typename T, std::size_t N>",
    "using AliasTable = std::array<AliasEntry<T>, N>;",
    "",
    "template <typename T, std::size_t N>",
    "using DeprecationTable = std::array<Deprecation<T>, N>;",
]

CPP_GENERIC_BLOCK = [
    "template <typename T, std::size_t N>",
    "constexpr void ResolveValues(const ValueTable<T, N>& entries,",
    "                             const Context& context, const Overrides& overrides,",
    "                             Tokens& tokens) {",
    "  for (const ValueEntry<T>& entry : entries) {",
    "    const T fallback = entry.defaults[Index(context, entry.modifiers)];",
    "    tokens.*entry.token = (overrides.*entry.value).value_or(fallback);",
    "  }",
    "}",
    "",
    "template <typename T, std::size_t N>",
    "constexpr void ResolveAliases(const AliasTable<T, N>& entries,",
    "                              const Overrides& overrides, Tokens& tokens) {",
    "  for (const AliasEntry<T>& entry : entries) {",
    "    const T target = tokens.*entry.target;",
    "    tokens.*entry.token = entry.value == nullptr",
    "                              ? target",
    "                              : (overrides.*entry.value).value_or(target);",
    "  }",
    "}",
    "",
    "template <typename T, std::size_t N>",
    "constexpr void ApplyDeprecations(const DeprecationTable<T, N>& entries,",
    "                                 Overrides& overrides) {",
    "  for (const Deprecation<T>& entry : entries) {",
    "    if (!(overrides.*entry.replacement).has_value()) {",
    "      overrides.*entry.replacement = overrides.*entry.deprecated;",
    "    }",
    "  }",
    "}",
    "",
    "template <typename Entries, typename Visitor>",
    "constexpr void VisitEntries(const Entries& entries, const Tokens& tokens,",
    "                            Visitor& visitor) {",
    "  for (const auto& entry : entries) {",
    "    visitor(entry.name, tokens.*entry.token);",
    "  }",
    "}",
]

CPP_API_BLOCK = [
    "[[nodiscard]] constexpr Tokens Resolve(const Context& context = {},",
    "                                       const Overrides& overrides = {}) {",
    "  return internal::ResolveAll(context, overrides);",
    "}",
    "",
    "template <typename Visitor>",
    "constexpr void VisitTokens(const Tokens& tokens, Visitor& visitor) {",
    "  internal::VisitAll(tokens, visitor);",
    "}",
    "",
    "}",
    "",
    "#endif",
    "",
]
