import argparse
import copy
import itertools
import json
import math
import pathlib
import re
import sys

NAME = re.compile(r"(?:[a-z][a-z0-9]*(?:_[a-z0-9]+)*|[0-9]+)\Z")
ALIAS = re.compile(r"\{([^{}]+)\}\Z")
SCHEMA = "https://www.designtokens.org/schemas/2025.10/format.json"
CONTEXTS = {
    "theme": ("dark", "light"),
    "contrast": ("normal", "high"),
    "density": ("phone", "tablet", "desktop", "tv"),
    "motion": ("full", "reduced"),
    "dynamic-range": ("sdr", "hdr"),
}
DEFAULTS = {
    "theme": "dark",
    "contrast": "normal",
    "density": "desktop",
    "motion": "full",
    "dynamic-range": "sdr",
}
CONTRASTS = {
    ("color", "text", "primary"): ({"color.surface.base", "color.surface.elevated"}, 4.5),
    ("color", "text", "secondary"): ({"color.surface.base", "color.surface.elevated"}, 4.5),
    ("color", "control", "foreground"): ({"color.control.background"}, 4.5),
    ("color", "focus", "ring"): ({"color.surface.base", "color.surface.elevated"}, 3),
}


def fail(message):
    raise ValueError(message)


def read_json(path):
    def unique_keys(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                fail(f"{path}: duplicate JSON property {key!r}")
            result[key] = value
        return result

    try:
        return json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=unique_keys,
            parse_constant=lambda value: fail(f"{path}: invalid JSON number {value}"),
        )
    except (OSError, json.JSONDecodeError) as error:
        fail(f"{path}: {error}")


def alias(value):
    return isinstance(value, str) and ALIAS.fullmatch(value) is not None


def validate_value(token_type, value, where):
    if alias(value):
        return
    if token_type == "color":
        if not isinstance(value, dict) or set(value) - {"colorSpace", "components", "alpha", "hex"}:
            fail(f"{where}: expected a DTCG color value object")
        components = value.get("components")
        if value.get("colorSpace") != "srgb" or not isinstance(components, list) or len(components) != 3:
            fail(f"{where}: authored colors must be three-component sRGB")
        if any(
            not isinstance(part, (int, float)) or isinstance(part, bool)
            or not math.isfinite(part) or not 0 <= part <= 1
            for part in components
        ):
            fail(f"{where}: sRGB components must be finite numbers from 0 through 1")
        alpha = value.get("alpha", 1)
        if not isinstance(alpha, (int, float)) or isinstance(alpha, bool) \
                or not math.isfinite(alpha) or not 0 <= alpha <= 1:
            fail(f"{where}: color alpha must be from 0 through 1")
        if "hex" in value and (
            not isinstance(value["hex"], str)
            or re.fullmatch(r"#[0-9a-fA-F]{6}", value["hex"]) is None
        ):
            fail(f"{where}: hex fallback must use six-digit CSS syntax")
    elif token_type == "dimension":
        if (
            not isinstance(value, dict) or set(value) != {"value", "unit"}
            or not isinstance(value["value"], (int, float))
            or isinstance(value["value"], bool) or not math.isfinite(value["value"])
            or not isinstance(value["unit"], str)
            or value["unit"] not in {"px", "rem"}
        ):
            fail(f"{where}: expected a DTCG dimension in px or rem")
    elif token_type == "fontFamily":
        if not isinstance(value, str) and not (
            isinstance(value, list) and all(isinstance(item, str) for item in value)
        ):
            fail(f"{where}: fontFamily must be a string or an array of strings")
    elif token_type == "fontWeight":
        allowed = {
            "thin", "hairline", "extra-light", "ultra-light", "light", "normal", "regular",
            "book", "medium", "semi-bold", "demi-bold", "bold", "extra-bold", "ultra-bold",
            "black", "heavy", "extra-black", "ultra-black",
        }
        numeric = (
            isinstance(value, (int, float)) and not isinstance(value, bool)
            and math.isfinite(value) and 1 <= value <= 1000
        )
        if not numeric and not (isinstance(value, str) and value in allowed):
            fail(f"{where}: invalid DTCG fontWeight")
    elif token_type == "duration":
        if (
            not isinstance(value, dict) or set(value) != {"value", "unit"}
            or not isinstance(value["value"], (int, float))
            or isinstance(value["value"], bool) or not math.isfinite(value["value"])
            or not isinstance(value["unit"], str)
            or value["unit"] not in {"ms", "s"}
        ):
            fail(f"{where}: expected a DTCG duration in ms or s")
    elif token_type == "cubicBezier":
        if (
            not isinstance(value, list) or len(value) != 4
            or any(not isinstance(point, (int, float)) or isinstance(point, bool)
                   or not math.isfinite(point) for point in value)
            or not 0 <= value[0] <= 1 or not 0 <= value[2] <= 1
        ):
            fail(f"{where}: invalid DTCG cubicBezier value")
    elif token_type == "number":
        if not isinstance(value, (int, float)) or isinstance(value, bool) \
                or not math.isfinite(value):
            fail(f"{where}: number token must contain a finite JSON number")
    elif token_type == "shadow":
        values = value if isinstance(value, list) else [value]
        required = {"color", "offsetX", "offsetY", "blur", "spread"}
        for shadow in values:
            if alias(shadow):
                continue
            if (
                not isinstance(shadow, dict) or not required.issubset(shadow)
                or set(shadow) - required - {"inset"}
                or ("inset" in shadow and not isinstance(shadow["inset"], bool))
            ):
                fail(f"{where}: invalid DTCG shadow structure")
            validate_value("color", shadow["color"], where + ".color")
            for key in ("offsetX", "offsetY", "blur", "spread"):
                validate_value("dimension", shadow[key], where + "." + key)
    else:
        fail(f"{where}: token type {token_type!r} is outside this base-set checker")


def tokens_in(node, path=(), inherited_type=None):
    if not isinstance(node, dict):
        fail(f"{'.'.join(path) or '<root>'}: group must be an object")
    allowed_group = {"$type", "$description", "$extensions", "$deprecated", "$root"}
    if not path:
        allowed_group.add("$schema")
    unknown = {key for key in node if key.startswith("$") and key not in allowed_group}
    if unknown:
        fail(f"{'.'.join(path) or '<root>'}: unsupported group properties {sorted(unknown)}")
    token_type = node.get("$type", inherited_type)
    if token_type is not None and not isinstance(token_type, str):
        fail(f"{'.'.join(path) or '<root>'}: $type must be a string")
    if "$description" in node and not isinstance(node["$description"], str):
        fail(f"{'.'.join(path) or '<root>'}: $description must be a string")
    if "$extensions" in node and not isinstance(node["$extensions"], dict):
        fail(f"{'.'.join(path) or '<root>'}: $extensions must be an object")
    if "$deprecated" in node and not isinstance(node["$deprecated"], (bool, str)):
        fail(f"{'.'.join(path) or '<root>'}: $deprecated must be boolean or string")
    for name, child in node.items():
        if name.startswith("$") and name != "$root":
            continue
        child_path = path + (name,)
        if name != "$root" and not NAME.fullmatch(name):
            fail(f"{'.'.join(child_path)}: invalid lower_snake path segment")
        if not isinstance(child, dict):
            fail(f"{'.'.join(child_path)}: token or group must be an object")
        if "$value" not in child:
            if name == "$root":
                fail(f"{'.'.join(child_path)}: $root must be a token")
            yield from tokens_in(child, child_path, token_type)
            continue
        allowed_token = {"$value", "$type", "$description", "$extensions", "$deprecated"}
        if set(child) - allowed_token:
            fail(f"{'.'.join(child_path)}: token has unsupported properties")
        current_type = child.get("$type", token_type)
        if not isinstance(current_type, str):
            fail(f"{'.'.join(child_path)}: token has no effective $type")
        if "$extensions" in child and not isinstance(child["$extensions"], dict):
            fail(f"{'.'.join(child_path)}: $extensions must be an object")
        if "$deprecated" in child and not isinstance(child["$deprecated"], (bool, str)):
            fail(f"{'.'.join(child_path)}: $deprecated must be boolean or string")
        validate_value(current_type, child["$value"], ".".join(child_path))
        yield child_path, current_type, child


def load_source(root, reference):
    if not isinstance(reference, dict) or set(reference) != {"$ref"} \
            or not isinstance(reference["$ref"], str):
        fail(f"unsupported token source reference: {reference!r}")
    path = (root / reference["$ref"]).resolve()
    if not path.is_relative_to(root.resolve()) or not path.is_file():
        fail(f"invalid or missing token source: {reference['$ref']}")
    data = read_json(path)
    if not isinstance(data, dict) or data.get("$schema") != SCHEMA:
        fail(f"{path}: missing DTCG Format 2025.10 schema reference")
    semantic = path.name in {"semantic.tokens.json", "contrast-high.tokens.json"}
    component = path.name == "components.tokens.json"
    for token_path, _, token in tokens_in(data):
        where = f"{path}:{'.'.join(token_path)}"
        description = token.get("$description")
        if (semantic or component) and (
            not isinstance(description, str) or not description.strip()
        ):
            fail(f"{where}: token requires a non-empty $description")
        extension = token.get("$extensions", {}).get("com.alumbra", {})
        if not isinstance(extension, dict):
            fail(f"{where}: com.alumbra extension must be an object")
        if component and (
            extension.get("tier") != "component"
            or extension.get("stability") not in {"stable", "experimental"}
        ):
            fail(f"{where}: component needs tier and stability metadata")
    return data


def merge(target, source):
    if not isinstance(target, dict) or not isinstance(source, dict) \
            or "$value" in target or "$value" in source:
        if isinstance(target, dict) and isinstance(source, dict) \
                and ("$value" in target) != ("$value" in source):
            fail("Resolver overlay changes a group into a token or a token into a group")
        return copy.deepcopy(source)
    result = copy.deepcopy(target)
    for key, value in source.items():
        result[key] = merge(result[key], value) if key in result else copy.deepcopy(value)
    return result


def resolver_inputs(resolver):
    modifiers = resolver.get("modifiers")
    if not isinstance(modifiers, dict) or set(modifiers) != set(CONTEXTS):
        fail("resolver must declare the five accepted modifiers")
    choices = []
    for name, contexts in CONTEXTS.items():
        modifier = modifiers[name]
        if (
            not isinstance(modifier, dict) or not isinstance(modifier.get("contexts"), dict)
            or set(modifier["contexts"]) != set(contexts)
            or modifier.get("default") != DEFAULTS[name]
        ):
            fail(f"invalid {name} contexts or default")
        choices.append(contexts)
    for values in itertools.product(*choices):
        yield dict(zip(CONTEXTS, values))


def resolve_tree(root, resolver, selection):
    result = {}
    seen = []
    for item in resolver.get("resolutionOrder", []):
        reference = item.get("$ref") if isinstance(item, dict) else None
        parts = reference.split("/") if isinstance(reference, str) else []
        if len(parts) != 3 or parts[0] != "#":
            fail(f"invalid Resolver order reference: {reference!r}")
        category, name = parts[1:]
        if category not in {"sets", "modifiers"} or name not in resolver.get(category, {}):
            fail(f"unknown Resolver order reference: {reference}")
        if (category, name) in seen:
            fail(f"duplicate Resolver order reference: {reference}")
        seen.append((category, name))
        item = resolver[category][name]
        sources = (
            item.get("sources") if category == "sets"
            else item["contexts"][selection[name]]
        )
        if not isinstance(sources, list):
            fail(f"Resolver sources for {name} must be an array")
        for source in sources:
            result = merge(result, load_source(root, source))
    expected_order = [("sets", "base")] + [
        ("modifiers", name) for name in CONTEXTS
    ]
    if seen != expected_order:
        fail("resolutionOrder must be base, then theme, contrast, density, motion and dynamic-range")
    return result


def resolve_tokens(tree):
    tokens = {path: (kind, token) for path, kind, token in tokens_in(tree)}

    def resolve_subvalue(value, expected, path, stack):
        if not alias(value):
            return value
        target = tuple(value[1:-1].split("."))
        target_kind, result = resolve(target, stack + (path,))
        if target_kind != expected:
            fail(f"{'.'.join(path)} shadow reference must be {expected}")
        return result

    def resolve(path, stack=()):
        if path in stack:
            fail(f"circular alias at {'.'.join(path)}")
        if path not in tokens:
            fail(f"unresolved alias target {'.'.join(path)}")
        kind, token = tokens[path]
        value = token["$value"]
        if alias(value):
            target = tuple(value[1:-1].split("."))
            target_kind, result = resolve(target, stack + (path,))
            if kind != target_kind:
                fail(f"{'.'.join(path)} aliases a {target_kind} token, expected {kind}")
            return kind, result
        if kind == "shadow":
            shadows = value if isinstance(value, list) else [value]
            resolved_shadows = []
            for shadow in shadows:
                if alias(shadow):
                    target = tuple(shadow[1:-1].split("."))
                    target_kind, shadow = resolve(target, stack + (path,))
                    if target_kind != "shadow":
                        fail(f"{'.'.join(path)} shadow reference must be shadow")
                else:
                    shadow = dict(shadow)
                    for key, expected in (
                        ("color", "color"), ("offsetX", "dimension"), ("offsetY", "dimension"),
                        ("blur", "dimension"), ("spread", "dimension"),
                    ):
                        shadow[key] = resolve_subvalue(shadow[key], expected, path, stack)
                resolved_shadows.append(shadow)
            return kind, resolved_shadows if isinstance(value, list) else resolved_shadows[0]
        return kind, value

    return {path: (*resolve(path), token) for path, (_, token) in tokens.items()}


def composite(foreground, background):
    fg, alpha = foreground["components"], foreground.get("alpha", 1)
    bg, bg_alpha = background["components"], background.get("alpha", 1)
    result_alpha = alpha + bg_alpha * (1 - alpha)
    components = [
        (fg[index] * alpha + bg[index] * bg_alpha * (1 - alpha)) / result_alpha
        for index in range(3)
    ]
    return {"colorSpace": "srgb", "components": components, "alpha": result_alpha}


def luminance(color):
    channels = [
        part / 12.92 if part <= 0.04045 else ((part + 0.055) / 1.055) ** 2.4
        for part in color["components"]
    ]
    return sum(weight * channel for weight, channel in zip((0.2126, 0.7152, 0.0722), channels))


def contrast_ratio(foreground, background, backdrop=None):
    if background.get("alpha", 1) < 1:
        if backdrop is None or backdrop.get("alpha", 1) < 1:
            fail("translucent background requires a declared opaque backdrop")
        background = composite(background, backdrop)
    else:
        background = composite(background, {"components": [0, 0, 0]})
    foreground = composite(foreground, background)
    first, second = luminance(foreground), luminance(background)
    return (max(first, second) + 0.05) / (min(first, second) + 0.05)


def check_contrast(resolved, selection, minimums):
    actual = {}
    for path, (kind, value, token) in resolved.items():
        meta = token.get("$extensions", {}).get("com.alumbra", {}).get("contrast")
        if meta:
            if not isinstance(meta, dict):
                fail(f"{'.'.join(path)} contrast metadata must be an object")
            actual[path] = (value, meta)
    if set(actual) != set(CONTRASTS):
        fail("declared contrast foregrounds do not match the reviewed pairs")
    for path, (background_names, threshold) in CONTRASTS.items():
        foreground, meta = actual[path]
        if set(meta.get("against", [])) != background_names or meta.get("minimum") != threshold:
            fail(f"{'.'.join(path)} has incorrect contrast metadata")
        for background_name in background_names:
            background_path = tuple(background_name.split("."))
            if background_path not in resolved or resolved[background_path][0] != "color":
                fail(f"invalid contrast background {background_name}")
            backdrop_name = meta.get("backdrop")
            backdrop = None
            if backdrop_name:
                backdrop_path = tuple(backdrop_name.split("."))
                if backdrop_path not in resolved or resolved[backdrop_path][0] != "color":
                    fail(f"invalid contrast backdrop {backdrop_name}")
                backdrop = resolved[backdrop_path][1]
            ratio = contrast_ratio(foreground, resolved[background_path][1], backdrop)
            key = (path, background_path, selection["theme"], selection["contrast"])
            minimums[key] = min(minimums.get(key, math.inf), ratio)
            if ratio + 1e-9 < threshold:
                fail(f"{'.'.join(path)} on {background_name} is {ratio:.3f}:1 in {selection}")


def run(root):
    token_root = (root / "design/tokens").resolve()
    resolver = read_json(token_root / "resolver.json")
    if not isinstance(resolver, dict):
        fail("Resolver document must be an object")
    if (
        resolver.get("$schema") != "https://www.designtokens.org/schemas/2025.10/resolver.json"
        or resolver.get("version") != "2025.10"
    ):
        fail("resolver must use DTCG Resolver 2025.10")
    base_sources = resolver.get("sets", {}).get("base", {}).get("sources", [])
    if not isinstance(base_sources, list) or len(base_sources) != 3:
        fail("base Resolver set must reference primitive, semantic and component sources")
    primitive, semantic, component = [
        load_source(token_root, source) for source in base_sources
    ]
    tiers = [
        {path: (kind, token) for path, kind, token in tokens_in(document)}
        for document in (primitive, semantic, component)
    ]
    primitive_paths, semantic_tokens, component_tokens = tiers
    semantic_paths, component_paths = set(semantic_tokens), set(component_tokens)
    primitive_paths = set(primitive_paths)
    if (
        primitive_paths & semantic_paths or primitive_paths & component_paths
        or semantic_paths & component_paths
    ):
        fail("token tiers must use distinct paths")
    sources = [
        ref["$ref"]
        for token_set in resolver.get("sets", {}).values()
        for ref in token_set.get("sources", [])
    ]
    sources.extend(
        ref["$ref"]
        for modifier in resolver.get("modifiers", {}).values()
        for context in modifier.get("contexts", {}).values()
        for ref in context
    )
    if len(sources) != len(set(sources)):
        fail("Resolver source references must not be duplicated")
    expected_sources = {
        "./" + path.relative_to(token_root).as_posix()
        for path in token_root.rglob("*.tokens.json")
    }
    if set(sources) != expected_sources:
        fail("Resolver source references must cover every token file exactly once")
    selections = list(resolver_inputs(resolver))
    if len(selections) != 64:
        fail(f"expected 64 Resolver permutations, found {len(selections)}")
    measurements = {}
    density_data = {}
    required_states = {
        ("player", "button", "background", "$root"),
        ("player", "button", "background", "hover"),
        ("player", "button", "background", "focus"),
        ("player", "button", "background", "pressed"),
        ("player", "button", "background", "disabled"),
    }
    if not required_states.issubset(component_paths):
        fail("button background requires a $root value and interaction states")
    for selection in selections:
        tree = resolve_tree(token_root, resolver, selection)
        resolved = resolve_tokens(tree)
        current = {path: (kind, token) for path, kind, token in tokens_in(tree)}
        for path in semantic_paths:
            token = current[path][1]
            value = token["$value"]
            if not alias(value):
                fail(f"semantic token {'.'.join(path)} must be an alias")
            target = tuple(value[1:-1].split("."))
            if target not in primitive_paths and not (
                token.get("$deprecated") and target in semantic_paths
            ):
                fail(f"semantic token {'.'.join(path)} must reference a primitive")
        for path in component_paths:
            value = current[path][1]["$value"]
            if not alias(value) or tuple(value[1:-1].split(".")) not in (
                semantic_paths | component_paths
            ):
                fail(f"component token {'.'.join(path)} must reference a semantic/component token")
        check_contrast(resolved, selection, measurements)
        if (
            selection["theme"], selection["contrast"], selection["motion"],
            selection["dynamic-range"],
        ) == ("dark", "normal", "full", "sdr"):
            density_data[selection["density"]] = (
                resolved[("typography", "font_size", "display")][1]["value"],
                resolved[("target", "minimum")][1]["value"],
                resolved[("spacing", "medium")][1]["value"],
            )
        ceiling = 600 if selection["dynamic-range"] == "hdr" else 203
        if resolved[("luminance", "reference_white")][1] != 203 \
                or resolved[("luminance", "hdr_highlight_ceiling")][1] != ceiling:
            fail(f"incorrect HDR luminance values in {selection}")
        durations = [
            resolved[("motion", "duration", name)][1]["value"]
            for name in ("instant", "short", "medium", "long")
        ]
        if selection["motion"] == "reduced" and any(durations):
            fail("reduced motion durations must resolve to zero")
        if selection["motion"] == "full" and durations[1] != 120:
            fail("full motion default duration must resolve to 120 ms")
    if set(density_data) != set(CONTEXTS["density"]) \
            or len(set(density_data.values())) != len(CONTEXTS["density"]):
        fail("each density requires distinct display, target and spacing values")
    if {key: value[1] for key, value in density_data.items()} != {
        "phone": 48, "tablet": 48, "desktop": 44, "tv": 72,
    }:
        fail("density minimum targets must be 44 px desktop, 48 px phone/tablet and 72 px TV")
    for (path, background, theme, contrast), ratio in sorted(measurements.items()):
        print(
            f"contrast theme={theme} contrast={contrast} {'.'.join(path)} / "
            f"{'.'.join(background)}: minimum {ratio:.2f}:1"
        )
    print(
        f"validated {len(primitive_paths)} primitive, {len(semantic_paths)} semantic and "
        f"{len(component_paths)} component tokens across {len(selections)} Resolver inputs"
    )
    print("contrast assumption: sRGB source-over compositing; declared backgrounds are opaque")
    print("density: phone, tablet, desktop and TV data are authored; renderer support is not asserted")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).parents[2])
    try:
        run(parser.parse_args().root.resolve())
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
