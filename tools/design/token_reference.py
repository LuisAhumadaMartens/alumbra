from token_model import CPP_TYPES, camel

NAMESPACE = "alumbra::design_tokens"
GAMUT_VALUES = (
    ("kSrgb", "sRGB, the baseline colour space."),
    ("kDisplayP3", "Display P3, a wide-gamut space."),
    ("kRec2020", "Rec. 2020, a wide-gamut space."),
)
STRUCT_FIELDS = (
    ("Rgb", (
        ("double", "red", "Red component, from 0 to 1."),
        ("double", "green", "Green component, from 0 to 1."),
        ("double", "blue", "Blue component, from 0 to 1."))),
    ("Color", (
        ("ColorSpace", "color_space", "Colour space of components."),
        ("Rgb", "components", "Components in color_space."),
        ("double", "alpha", "Opacity, from 0 to 1."),
        ("Rgb", "srgb", "The same colour converted to sRGB, for targets without wide gamut."))),
    ("CubicBezier", (
        ("double", "x1", "Abscissa of the first control point."),
        ("double", "y1", "Ordinate of the first control point."),
        ("double", "x2", "Abscissa of the second control point."),
        ("double", "y2", "Ordinate of the second control point."))),
    ("ShadowLayer", (
        ("Color", "color", "Shadow colour."),
        ("double", "offset_x", "Horizontal offset in pixels."),
        ("double", "offset_y", "Vertical offset in pixels."),
        ("double", "blur", "Blur radius in pixels."),
        ("double", "spread", "Spread radius in pixels."),
        ("bool", "inset", "Whether the shadow is drawn inside the box."))),
)
STRUCT_BRIEFS = {
    "Rgb": "Red, green and blue components of a colour.",
    "Color": "Colour with its colour space, alpha and sRGB fallback.",
    "CubicBezier": "Control points of a cubic Bezier easing curve.",
    "ShadowLayer": "One layer of a box shadow.",
}
ALIASES = (
    ("Pixels", "Length in CSS pixels."),
    ("Milliseconds", "Duration in milliseconds as a double."),
    ("FontFamily", "Ordered font family names, most preferred first."),
    ("Shadow", "Shadow layers, outermost first."),
)


def render_reference(catalogue, command):
    namespace = NAMESPACE
    lines = [
        "\\file alumbra/design_tokens/design_tokens.h",
        "Design tokens as constexpr values, resolved for a context.",
        "",
        f"Generated from design/tokens/ by `{command}`; do not edit it by hand.",
        "",
        "\\namespace alumbra",
        "Alumbra's top-level namespace for design tokens.",
        "",
        f"\\namespace {namespace}",
        "Design tokens for C++ renderers: contexts, value types and the resolved token set.",
        "",
        "Resolve() evaluates every token for a Context, applying aliases and optional Overrides. The",
        "[design token reference](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/reference/design-tokens.md)",
        "lists every token, its type and its default.",
        "",
    ]
    for modifier in catalogue.modifiers:
        name = modifier.type_name
        lines += [
            f"\\enum {namespace}::{name}",
            f"Values of the {modifier.name} modifier of a Context.",
            "",
        ]
        for context in modifier.contexts:
            default = " This is the default." if context == modifier.default else ""
            lines += [
                f"\\var {namespace}::{name}::k{camel(context, True)}",
                f"The {context} context.{default}",
                "",
            ]
    lines += [
        f"\\struct {namespace}::Context",
        "One value of every modifier, which selects the resolved value of each token.",
        "",
    ]
    for modifier in catalogue.modifiers:
        lines += [
            f"\\var {modifier.type_name} {namespace}::Context::{modifier.snake}",
            f"The {modifier.name} context; {modifier.default} by default.",
            "",
        ]
    for modifier in catalogue.modifiers:
        name = modifier.type_name
        lines += [
            f"\\var {namespace}::k{name}Values",
            f"Every {name} value, in declaration order.",
            "",
            f"\\fn std::string_view {namespace}::Name({name} value)",
            f"Returns the name of a {name} value as written in the token files and the CSS selectors.",
            "",
        ]
    lines += [
        f"\\enum {namespace}::ColorSpace",
        "Colour space of a Color.",
        "",
    ]
    for value, text in GAMUT_VALUES:
        lines += [f"\\var {namespace}::ColorSpace::{value}", text, ""]
    for struct, fields in STRUCT_FIELDS:
        lines += [f"\\struct {namespace}::{struct}", STRUCT_BRIEFS[struct], ""]
        for kind, field, text in fields:
            lines += [f"\\var {kind} {namespace}::{struct}::{field}", text, ""]
    for alias, text in ALIASES:
        lines += [f"\\typedef {namespace}::{alias}", text, ""]
    lines += [
        f"\\struct {namespace}::Tokens",
        "Resolved value of every public token for one context.",
        "",
        "Member names are the token identifiers; the token reference maps each one to its token name.",
        "",
    ]
    for token in catalogue.public:
        lines += [
            f"\\var {CPP_TYPES[token.kind]} {namespace}::Tokens::{token.identifier}",
            token.description or f"Value of the token {token.name}.",
            "",
        ]
    lines += [
        f"\\struct {namespace}::Overrides",
        "Values that replace resolved tokens; an empty member leaves the token as resolved.",
        "",
        "An override wins over the value a token would derive through an alias.",
        "",
    ]
    for token in catalogue.public:
        lines += [
            f"\\var std::optional<{CPP_TYPES[token.kind]}> {namespace}::Overrides::{token.identifier}",
            f"Replacement for the token {token.name}.",
            "",
        ]
    lines += [
        f"\\fn Tokens {namespace}::Resolve(const Context& context, const Overrides& overrides)",
        "Resolves every public token for a context, then applies the overrides.",
        "",
        "Both arguments default to the default context with no overrides. The result is a constant expression.",
        "",
        f"\\fn void {namespace}::VisitTokens(const Tokens& tokens, Visitor& visitor)",
        "Calls the visitor once for each public token with its name and resolved value.",
        "",
    ]
    return "\n".join(lines).rstrip("\n") + "\n"
