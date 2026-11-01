import decimal
import itertools
import pathlib

import check_tokens as source
from color_gamut import srgb_fallback

ROOT = pathlib.Path(__file__).resolve().parents[2]
CSS_PATH = "antumbra/generated/design-tokens.css"
TS_PATH = "antumbra/generated/DesignTokens.ts"
SWIFT_PATH = "antumbra/generated/DesignTokens.swift"
KOTLIN_PATH = "antumbra/generated/DesignTokens.kt"
CPP_PATH = "antumbra/design_tokens/include/alumbra/design_tokens/design_tokens.h"
DOCS_PATH = "docs/reference/design-tokens.md"
COMMAND = "python3 tools/design/generate_tokens.py"
TIERS = ("primitive", "semantic", "component")
PUBLIC_TIERS = ("semantic", "component")
REM_PIXELS = 16
FONT_WEIGHTS = {
    "thin": 100, "hairline": 100, "extra-light": 200, "ultra-light": 200,
    "light": 300, "normal": 400, "regular": 400, "book": 400, "medium": 500,
    "semi-bold": 600, "demi-bold": 600, "bold": 700, "extra-bold": 800,
    "ultra-bold": 800, "black": 900, "heavy": 900, "extra-black": 950,
    "ultra-black": 950,
}
GENERIC_FAMILIES = frozenset({
    "serif", "sans-serif", "monospace", "cursive", "fantasy", "system-ui",
    "ui-serif", "ui-sans-serif", "ui-monospace", "ui-rounded", "math", "emoji",
    "fangsong",
})
MEDIA = {
    ("theme", "light"): "(prefers-color-scheme: light)",
    ("contrast", "high"): "(prefers-contrast: more)",
    ("motion", "reduced"): "(prefers-reduced-motion: reduce)",
}
WIDE_GAMUTS = {
    "display-p3": ("(color-gamut: p3)", "(color: color(display-p3 0 0 0))"),
    "rec2020": ("(color-gamut: rec2020)", "(color: color(rec2020 0 0 0))"),
}
CPP_TYPES = {
    "color": "Color", "dimension": "Pixels", "duration": "Milliseconds",
    "number": "double", "fontWeight": "double", "fontFamily": "FontFamily",
    "cubicBezier": "CubicBezier", "shadow": "Shadow",
}
TS_TYPES = {
    "color": "Color", "dimension": "number", "duration": "number",
    "number": "number", "fontWeight": "number", "fontFamily": "FontFamily",
    "cubicBezier": "CubicBezier", "shadow": "Shadow",
}
SWIFT_TYPES = {
    "color": "Color", "dimension": "Double", "duration": "Double",
    "number": "Double", "fontWeight": "Double", "fontFamily": "[String]",
    "cubicBezier": "CubicBezier", "shadow": "[ShadowLayer]",
}
KOTLIN_TYPES = {
    "color": "Color", "dimension": "Double", "duration": "Double",
    "number": "Double", "fontWeight": "Double", "fontFamily": "List<String>",
    "cubicBezier": "CubicBezier", "shadow": "List<ShadowLayer>",
}
COLUMNS = 80


def fail(message):
    raise ValueError(message)


def display(path):
    return ".".join(path)


def css_name(segments):
    return "--alumbra-" + "-".join(segments)


def identifier(segments):
    return "alumbra_token_" + "_s".join(part.replace("_", "_u") for part in segments)


def natural_key(segments):
    return tuple((0, int(part), "") if part.isdigit() else (1, 0, part) for part in segments)


def camel(name, upper):
    words = [word for word in name.replace("-", "_").split("_") if word]
    text = "".join(word[:1].upper() + word[1:] for word in words)
    return text if upper else text[:1].lower() + text[1:]


def alias_path(value):
    return tuple(value[1:-1].split(".")) if source.alias(value) else None


def to_float(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        fail(f"expected a number, found {value!r}")
    return float(value) + 0.0


def scaled(value, factor):
    return float(decimal.Decimal(repr(to_float(value))) * factor) + 0.0


def decimal_text(value):
    text = format(value.normalize(), "f")
    return "0" if text in {"0", "-0"} else text


def number_text(value):
    return decimal_text(decimal.Decimal(repr(to_float(value))))


def percent_text(value):
    return decimal_text(decimal.Decimal(repr(to_float(value))) * 100) + "%"


def literal(value):
    return repr(to_float(value))


def canonical(kind, value):
    if kind == "color":
        space = value["colorSpace"]
        components = [to_float(part) for part in value["components"]]
        return {
            "colorSpace": space,
            "components": components,
            "alpha": to_float(value.get("alpha", 1)),
            "srgb": list(srgb_fallback(space, components)),
        }
    if kind == "dimension":
        if value["unit"] == "px":
            return to_float(value["value"])
        return scaled(value["value"], REM_PIXELS)
    if kind == "duration":
        if value["unit"] == "ms":
            return to_float(value["value"])
        return scaled(value["value"], 1000)
    if kind in {"number", "fontWeight"}:
        return to_float(FONT_WEIGHTS.get(value, value) if isinstance(value, str) else value)
    if kind == "fontFamily":
        return [value] if isinstance(value, str) else list(value)
    if kind == "cubicBezier":
        return [to_float(part) for part in value]
    if kind == "shadow":
        layers = []
        for layer in value if isinstance(value, list) else [value]:
            if isinstance(layer, list):
                layers.extend(canonical("shadow", layer))
                continue
            layers.append({
                "color": canonical("color", layer["color"]),
                "offsetX": canonical("dimension", layer["offsetX"]),
                "offsetY": canonical("dimension", layer["offsetY"]),
                "blur": canonical("dimension", layer["blur"]),
                "spread": canonical("dimension", layer["spread"]),
                "inset": bool(layer.get("inset", False)),
            })
        return layers
    fail(f"token type {kind!r} has no generated representation")


class Modifier:
    def __init__(self, name, contexts, default):
        self.name = name
        self.contexts = tuple(contexts)
        self.default = default
        self.default_index = self.contexts.index(default)
        self.type_name = camel(name, True)
        self.field = camel(name, False)
        self.snake = name.replace("-", "_")


class Token:
    def __init__(self, path, tier, kind):
        self.path = path
        self.segments = tuple(part for part in path if part != "$root")
        self.name = display(self.segments)
        self.tier = tier
        self.kind = kind
        self.description = ""
        self.deprecated = False
        self.css = css_name(self.segments)
        self.identifier = identifier(self.segments)
        self.values = []
        self.raw = []
        self.alias = None
        self.modifiers = ()
        self.table = []
        self.depth = 0
        self.replacement = None

    @property
    def public(self):
        return self.tier in PUBLIC_TIERS


class Catalogue:
    def __init__(self, modifiers, selections, tokens):
        self.modifiers = modifiers
        self.selections = selections
        self.tokens = tokens
        self.by_path = {token.path: token for token in tokens}
        self.by_name = {token.name: token for token in tokens}
        self.public = sorted(
            (token for token in tokens if token.public),
            key=lambda token: (token.depth, natural_key(token.segments)))
        self.deprecations = sorted(
            (token for token in self.public if token.replacement is not None),
            key=lambda token: (-token.depth, natural_key(token.segments)))

    def selection_index(self, contexts):
        index = 0
        for modifier in self.modifiers:
            index = index * len(modifier.contexts) + modifier.contexts.index(contexts[modifier.name])
        return index

    @property
    def default_index(self):
        return self.selection_index({modifier.name: modifier.default for modifier in self.modifiers})


def check_collisions(tokens):
    for label, attribute in (
            ("canonical name", "name"), ("CSS custom property", "css"),
            ("identifier", "identifier")):
        seen = {}
        for token in tokens:
            value = getattr(token, attribute)
            if value in seen:
                fail(
                    f"{label} {value} is generated by both {seen[value]} and "
                    f"{display(token.path)}")
            seen[value] = display(token.path)


def load(token_root):
    token_root = token_root.resolve()
    resolver = source.read_json(token_root / "resolver.json")
    if not isinstance(resolver, dict) or (
        resolver.get("$schema") != "https://www.designtokens.org/schemas/2025.10/resolver.json"
        or resolver.get("version") != "2025.10"
    ):
        fail("resolver must use DTCG Resolver 2025.10")
    base_sources = resolver.get("sets", {}).get("base", {}).get("sources", [])
    if not isinstance(base_sources, list) or len(base_sources) != len(TIERS):
        fail("base Resolver set must reference primitive, semantic and component sources")
    tiers = {}
    for tier, reference in zip(TIERS, base_sources):
        for path, _, _ in source.tokens_in(source.load_source(token_root, reference)):
            if path in tiers:
                fail(f"{display(path)} is defined in more than one tier")
            tiers[path] = tier
    selections = list(source.resolver_inputs(resolver))
    modifiers = [
        Modifier(name, contexts, source.DEFAULTS[name])
        for name, contexts in source.CONTEXTS.items()
    ]
    defaults = {modifier.name: modifier.default for modifier in modifiers}
    tokens = {}
    for selection in selections:
        tree = source.resolve_tree(token_root, resolver, selection)
        entries = {path: (kind, token) for path, kind, token in source.tokens_in(tree)}
        if set(entries) != set(tiers):
            fail(f"Resolver contexts must not add or remove tokens: {selection}")
        resolved = source.resolve_tokens(tree)
        for path in sorted(entries, key=natural_key):
            kind, token = entries[path]
            if path not in tokens:
                tokens[path] = Token(path, tiers[path], kind)
            current = tokens[path]
            if current.kind != kind:
                fail(f"{display(path)} changes $type between Resolver contexts")
            if selection == defaults:
                current.description = token.get("$description", "")
                current.deprecated = token.get("$deprecated", False)
            current.values.append(canonical(kind, resolved[path][1]))
            current.raw.append(token["$value"])
    ordered = sorted(tokens.values(), key=lambda token: natural_key(token.path))
    check_collisions(ordered)
    catalogue_tokens = {token.path: token for token in ordered}
    for token in ordered:
        if token.public:
            classify(token, catalogue_tokens, modifiers)
    by_name = {token.name: token for token in ordered}
    for token in ordered:
        if token.public:
            token.depth = depth(token, by_name)
    return Catalogue(modifiers, selections, ordered)


def classify(token, tokens, modifiers):
    targets = set()
    for raw in token.raw:
        path = alias_path(raw)
        targets.add(path if path is not None and tokens[path].public else None)
    if len(targets) != 1:
        fail(
            f"{token.name} changes its public alias target between Resolver contexts; "
            "generated outputs require a context-independent alias")
    target = targets.pop()
    if target is not None:
        target_token = tokens[target]
        if target_token.kind != token.kind:
            fail(f"{token.name} aliases a {target_token.kind} token")
        token.alias = target_token.name
        if token.deprecated and target_token.tier == token.tier:
            token.replacement = target_token.name
        return
    sizes = [len(modifier.contexts) for modifier in modifiers]
    combinations = list(itertools.product(*[range(size) for size in sizes]))
    position = {combination: index for index, combination in enumerate(combinations)}
    relevant = []
    for axis, modifier in enumerate(modifiers):
        for combination in combinations:
            changed = False
            for context in range(sizes[axis]):
                other = list(combination)
                other[axis] = context
                if token.values[position[tuple(other)]] != token.values[position[combination]]:
                    changed = True
                    break
            if changed:
                relevant.append(modifier)
                break
    token.modifiers = tuple(relevant)
    table = []
    for choice in itertools.product(*[range(len(modifier.contexts)) for modifier in relevant]):
        combination = [modifier.default_index for modifier in modifiers]
        for modifier, context in zip(relevant, choice):
            combination[modifiers.index(modifier)] = context
        table.append(token.values[position[tuple(combination)]])
    token.table = table
    for combination in combinations:
        index = 0
        for modifier in relevant:
            axis = modifiers.index(modifier)
            index = index * sizes[axis] + combination[axis]
        if table[index] != token.values[position[combination]]:
            fail(f"{token.name} values are not a function of their modifiers")


def depth(token, by_name):
    count = 0
    while token.alias is not None:
        token = by_name[token.alias]
        count += 1
    return count


def gamuts(kind, raw):
    if source.alias(raw):
        return set()
    if kind == "color":
        return {raw["colorSpace"]} - {"srgb"}
    if kind == "shadow":
        result = set()
        for layer in raw if isinstance(raw, list) else [raw]:
            if isinstance(layer, dict) and not source.alias(layer["color"]):
                result.add(layer["color"]["colorSpace"])
        return result - {"srgb"}
    return set()


class CssWriter:
    def __init__(self, catalogue):
        self.catalogue = catalogue

    def reference(self, raw):
        return f"var({self.catalogue.by_path[alias_path(raw)].css})"

    def color(self, raw, wide):
        if source.alias(raw):
            return self.reference(raw)
        value = canonical("color", raw)
        alpha = "" if value["alpha"] == 1 else f" / {percent_text(value['alpha'])}"
        if wide and value["colorSpace"] != "srgb":
            parts = " ".join(number_text(part) for part in value["components"])
            return f"color({value['colorSpace']} {parts}{alpha})"
        parts = " ".join(percent_text(part) for part in value["srgb"])
        return f"rgb({parts}{alpha})"

    def dimension(self, raw):
        if source.alias(raw):
            return self.reference(raw)
        return f"{number_text(raw['value'])}{raw['unit']}"

    def value(self, token, raw, wide=False):
        if source.alias(raw):
            return self.reference(raw)
        kind = token.kind
        if kind == "color":
            return self.color(raw, wide)
        if kind == "dimension":
            return self.dimension(raw)
        if kind == "duration":
            return f"{number_text(raw['value'])}{raw['unit']}"
        if kind in {"number", "fontWeight"}:
            return number_text(canonical(kind, raw))
        if kind == "fontFamily":
            names = [raw] if isinstance(raw, str) else raw
            return ", ".join(
                name if name in GENERIC_FAMILIES
                else '"' + name.replace("\\", "\\\\").replace('"', '\\"') + '"'
                for name in names)
        if kind == "cubicBezier":
            return "cubic-bezier(" + ", ".join(number_text(part) for part in raw) + ")"
        if kind == "shadow":
            layers = []
            for layer in raw if isinstance(raw, list) else [raw]:
                if source.alias(layer):
                    layers.append(self.reference(layer))
                    continue
                parts = [self.dimension(layer[key]) for key in ("offsetX", "offsetY", "blur", "spread")]
                text = " ".join(parts + [self.color(layer["color"], wide)])
                layers.append(("inset " if layer.get("inset") else "") + text)
            return ", ".join(layers)
        fail(f"token type {kind!r} has no CSS representation")

    def declarations(self, selection, paths):
        tokens = self.catalogue.by_path
        result = []
        for path in paths:
            token = tokens[path]
            if token.replacement is not None:
                continue
            raw = token.raw[selection]
            result.append((token, raw, self.deprecated(token, self.value(token, raw))))
        return result

    def deprecated(self, token, value):
        for item in reversed(self.catalogue.deprecations):
            if item.replacement == token.name:
                value = self.deprecated(item, f"var({item.css}, {value})")
        return value

    def render(self):
        catalogue = self.catalogue
        default = catalogue.default_index
        paths = [
            token.path for token in sorted(
                catalogue.tokens,
                key=lambda item: (TIERS.index(item.tier), natural_key(item.segments)))
        ]
        blocks = [((), (":root", ":host"), self.declarations(default, paths))]
        for modifier in catalogue.modifiers:
            for context in modifier.contexts:
                if context == modifier.default:
                    continue
                contexts = {item.name: item.default for item in catalogue.modifiers}
                contexts[modifier.name] = context
                selection = catalogue.selection_index(contexts)
                changed = [
                    path for path in paths
                    if catalogue.by_path[path].raw[selection] != catalogue.by_path[path].raw[default]
                ]
                declarations = self.declarations(selection, changed)
                attribute = f"data-alumbra-{modifier.name}"
                media = MEDIA.get((modifier.name, context))
                if media:
                    blocks.append(((media,), (
                        f":root:not([{attribute}])", f":host(:not([{attribute}]))"),
                        declarations))
                blocks.append(((), (
                    f':root[{attribute}="{context}"]', f':host([{attribute}="{context}"])'),
                    declarations))
        output = []
        for media, selectors, declarations in blocks:
            if not declarations:
                continue
            body = [f"  {token.css}: {value};" for token, _, value in declarations]
            output.append(nest(list(media), [], selectors, body))
            for gamut, (gamut_media, supports) in WIDE_GAMUTS.items():
                wide = [
                    f"  {token.css}: {self.deprecated(token, self.value(token, raw, True))};"
                    for token, raw, _ in declarations
                    if gamut in gamuts(token.kind, raw)
                    and max(gamuts(token.kind, raw), key=list(WIDE_GAMUTS).index) == gamut
                ]
                if wide:
                    output.append(nest(list(media) + [gamut_media], [supports], selectors, wide))
        return "\n".join(output)


def nest(media, supports, selectors, body):
    lines = [selector + "," for selector in selectors[:-1]]
    lines += [selectors[-1] + " {", *body, "}"]
    for condition in supports:
        lines = [f"@supports {condition} {{", *["  " + line for line in lines], "}"]
    if media:
        lines = [f"@media {' and '.join(media)} {{", *["  " + line for line in lines], "}"]
    return "\n".join(lines) + "\n"
