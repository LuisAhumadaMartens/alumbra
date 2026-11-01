import argparse
import decimal
import json
import pathlib
import re
import subprocess
import sys

from token_languages import override_chain
from token_model import CSS_PATH, MEDIA, REM_PIXELS, ROOT, WIDE_GAMUTS, load

SENTINELS = {
    "color.control.background": (0.25, 0.5, 0.75, 1.0),
    "player.button.background.focus": (0.125, 0.25, 0.375, 0.5),
}
SCENARIOS = {
    "semantic": ("color.control.background",),
    "component": ("color.control.background", "player.button.background.focus"),
}
GAMUT_SPACES = {"srgb": (), "p3": ("display-p3",), "rec2020": ("display-p3", "rec2020")}
SELECTOR = re.compile(
    r':(?P<scope>root|host)'
    r'(?:\[data-alumbra-(?P<rooted>[a-z-]+)="(?P<rooted_value>[a-z0-9-]+)"\]'
    r'|:not\(\[data-alumbra-(?P<rooted_absent>[a-z-]+)\]\)'
    r'|\(\[data-alumbra-(?P<hosted>[a-z-]+)="(?P<hosted_value>[a-z0-9-]+)"\]\)'
    r'|\(:not\(\[data-alumbra-(?P<hosted_absent>[a-z-]+)\]\)\))?\Z')
NUMBER = r"-?(?:\d+\.?\d*|\.\d+)"


def fail(message):
    raise ValueError(message)


def sentinel(name):
    red, green, blue, alpha = SENTINELS[name]
    return {
        "colorSpace": "srgb", "components": [red, green, blue], "alpha": alpha,
        "srgb": [red, green, blue],
    }


def context_key(catalogue, index):
    keys = []
    for modifier in reversed(catalogue.modifiers):
        index, position = divmod(index, len(modifier.contexts))
        keys.append(modifier.contexts[position])
    return "/".join(reversed(keys))


def expected_scenario(catalogue, overrides):
    default = catalogue.default_index
    values = {}
    for token in catalogue.public:
        if token.replacement is not None:
            values[token.name] = values[token.alias]
            continue
        chain = [item.name for item in override_chain(catalogue, token)]
        override = next((overrides[name] for name in chain if name in overrides), None)
        if override is not None:
            values[token.name] = override
        elif token.alias is not None:
            values[token.name] = values[token.alias]
        else:
            values[token.name] = token.values[default]
    return values


def compare(expected, actual, where, problems):
    if set(expected) != set(actual):
        missing = sorted(set(expected) - set(actual))
        extra = sorted(set(actual) - set(expected))
        problems.append(f"{where}: missing {missing[:5]}, unexpected {extra[:5]}")
    for name in sorted(set(expected) & set(actual)):
        if expected[name] != actual[name]:
            problems.append(f"{where}: {name} is {actual[name]!r}, expected {expected[name]!r}")


def check_dump(catalogue, document):
    problems = []
    contexts = document.get("contexts", {})
    expected_keys = [context_key(catalogue, index) for index in range(len(catalogue.selections))]
    if sorted(contexts) != sorted(expected_keys):
        problems.append(f"contexts are {sorted(contexts)[:3]}..., expected all {len(expected_keys)}")
    for index, key in enumerate(expected_keys):
        expected = {token.name: token.values[index] for token in catalogue.public}
        compare(expected, contexts.get(key, {}), key, problems)
    scenarios = document.get("scenarios", {})
    for name, overrides in SCENARIOS.items():
        expected = expected_scenario(
            catalogue, {override: sentinel(override) for override in overrides})
        compare(expected, scenarios.get(name, {}), f"scenario {name}", problems)
    return problems


def split_top(text, separator):
    parts, depth, quote, current = [], 0, None, []
    for character in text:
        if quote:
            current.append(character)
            if character == quote:
                quote = None
            continue
        if character in "\"'":
            quote = character
        elif character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
        elif character == separator and depth == 0:
            parts.append("".join(current).strip())
            current = []
            continue
        current.append(character)
    parts.append("".join(current).strip())
    return [part for part in parts if part]


def parse_css(text):
    rules = []

    def block(position, media, supports):
        while True:
            while position < len(text) and text[position].isspace():
                position += 1
            if position >= len(text):
                return position
            if text[position] == "}":
                return position + 1
            brace = text.index("{", position)
            prelude = " ".join(text[position:brace].split())
            if prelude.startswith("@media "):
                position = block(brace + 1, media + [prelude[7:]], supports)
            elif prelude.startswith("@supports "):
                position = block(brace + 1, media, supports + [prelude[10:]])
            elif prelude.startswith("@"):
                fail(f"unsupported at-rule {prelude}")
            else:
                end = text.index("}", brace)
                declarations = []
                for item in split_top(text[brace + 1:end], ";"):
                    name, _, value = item.partition(":")
                    declarations.append((name.strip(), value.strip()))
                rules.append({
                    "media": media, "supports": supports,
                    "selectors": split_top(prelude, ","), "declarations": declarations,
                    "order": len(rules),
                })
                position = end + 1

    if block(0, [], []) < len(text):
        fail("unbalanced braces in CSS output")
    return rules


def selector_match(selector, environment):
    found = SELECTOR.fullmatch(selector)
    if not found:
        fail(f"unsupported selector {selector}")
    if found["scope"] != environment["scope"]:
        return None
    attribute = found["rooted"] or found["hosted"]
    absent = found["rooted_absent"] or found["hosted_absent"]
    if found["scope"] == "host" and found["rooted"] or found["scope"] == "root" and found["hosted"]:
        fail(f"malformed scope selector {selector}")
    if attribute:
        value = found["rooted_value"] or found["hosted_value"]
        return (0, 2, 0) if environment["attributes"].get(attribute) == value else None
    if absent:
        return (0, 2, 0) if absent not in environment["attributes"] else None
    return (0, 1, 0)


def media_match(condition, environment):
    for feature in condition.split(" and "):
        if feature == "(color-gamut: p3)":
            matched = environment["gamut"] in {"p3", "rec2020"}
        elif feature == "(color-gamut: rec2020)":
            matched = environment["gamut"] == "rec2020"
        elif feature in MEDIA.values():
            matched = feature in environment["preferences"]
        else:
            fail(f"unsupported media feature {feature}")
        if not matched:
            return False
    return True


def supports_match(condition, environment):
    if condition not in {supports for _, supports in WIDE_GAMUTS.values()}:
        fail(f"unsupported @supports condition {condition}")
    return environment["gamut"] != "srgb"


def cascade(rules, environment):
    winners = {}
    for rule in rules:
        if not all(media_match(item, environment) for item in rule["media"]):
            continue
        if not all(supports_match(item, environment) for item in rule["supports"]):
            continue
        specificity = max(
            (match for match in (selector_match(item, environment) for item in rule["selectors"])
             if match is not None), default=None)
        if specificity is None:
            continue
        for name, value in rule["declarations"]:
            rank = (specificity, rule["order"])
            if name not in winners or rank >= winners[name][0]:
                winners[name] = (rank, value)
    return {name: value for name, (_, value) in winners.items()}


def substitute(value, properties, stack=()):
    result = []
    position = 0
    while True:
        start = value.find("var(", position)
        if start < 0:
            result.append(value[position:])
            return "".join(result)
        result.append(value[position:start])
        depth, end = 0, start + 3
        while True:
            if value[end] == "(":
                depth += 1
            elif value[end] == ")":
                depth -= 1
                if depth == 0:
                    break
            end += 1
        arguments = split_top(value[start + 4:end], ",")
        name = arguments[0]
        fallback = value[start + 4:end].split(",", 1)[1].strip() if len(arguments) > 1 else None
        if name in stack:
            fail(f"circular custom property {name}")
        if name in properties:
            result.append(substitute(properties[name], properties, stack + (name,)))
        elif fallback is not None:
            result.append(substitute(fallback, properties, stack))
        else:
            fail(f"{name} is not defined")
        position = end + 1


def css_number(text):
    if not re.fullmatch(NUMBER, text):
        fail(f"invalid CSS number {text}")
    return decimal.Decimal(text)


def to_float(value):
    return float(value) + 0.0


def css_color(text):
    found = re.fullmatch(r"rgb\((\S+)% (\S+)% (\S+)%(?: / (\S+)%)?\)", text)
    if found:
        components = [to_float(css_number(part) / 100) for part in found.groups()[:3]]
        alpha = 1.0 if found[4] is None else to_float(css_number(found[4]) / 100)
        return "srgb", components, alpha
    found = re.fullmatch(r"color\((display-p3|rec2020) (\S+) (\S+) (\S+)(?: / (\S+)%)?\)", text)
    if found:
        components = [to_float(css_number(part)) for part in found.groups()[1:4]]
        alpha = 1.0 if found[5] is None else to_float(css_number(found[5]) / 100)
        return found[1], components, alpha
    fail(f"invalid CSS color {text}")


def css_length(text):
    found = re.fullmatch(rf"({NUMBER})(px|rem)", text)
    if not found:
        fail(f"invalid CSS length {text}")
    scale = 1 if found[2] == "px" else REM_PIXELS
    return to_float(css_number(found[1]) * scale)


def css_value(kind, text):
    if kind == "color":
        return css_color(text)
    if kind == "dimension":
        return css_length(text)
    if kind == "duration":
        found = re.fullmatch(rf"({NUMBER})(ms|s)", text)
        if not found:
            fail(f"invalid CSS time {text}")
        return to_float(css_number(found[1]) * (1 if found[2] == "ms" else 1000))
    if kind in {"number", "fontWeight"}:
        return to_float(css_number(text))
    if kind == "fontFamily":
        names = []
        for name in split_top(text, ","):
            if name.startswith('"'):
                name = re.sub(r"\\(.)", r"\1", name[1:-1])
            names.append(name)
        return names
    if kind == "cubicBezier":
        found = re.fullmatch(r"cubic-bezier\((.*)\)", text)
        if not found:
            fail(f"invalid CSS cubic-bezier {text}")
        return [to_float(css_number(part)) for part in split_top(found[1], ",")]
    if kind == "shadow":
        layers = []
        for layer in split_top(text, ","):
            inset = layer.startswith("inset ")
            parts = layer[6:] if inset else layer
            lengths = parts.split(" ", 4)
            layers.append({
                "color": css_color(lengths[4]),
                "offsetX": css_length(lengths[0]),
                "offsetY": css_length(lengths[1]),
                "blur": css_length(lengths[2]),
                "spread": css_length(lengths[3]),
                "inset": inset,
            })
        return layers
    fail(f"token type {kind} has no CSS parser")


def displayed(color, gamut):
    if color["colorSpace"] in GAMUT_SPACES[gamut]:
        return color["colorSpace"], color["components"], color["alpha"]
    return "srgb", color["srgb"], color["alpha"]


def expected_css(kind, value, gamut):
    if kind == "color":
        return displayed(value, gamut)
    if kind == "shadow":
        return [{**layer, "color": displayed(layer["color"], gamut)} for layer in value]
    return value


def environments(catalogue, index):
    contexts = context_key(catalogue, index).split("/")
    explicit = {modifier.name: context for modifier, context in zip(catalogue.modifiers, contexts)}
    preferred, attributes = set(), {}
    for modifier, context in zip(catalogue.modifiers, contexts):
        media = MEDIA.get((modifier.name, context))
        if media:
            preferred.add(media)
        elif context != modifier.default or not any(
                name == modifier.name for name, _ in MEDIA):
            attributes[modifier.name] = context
    for scope in ("root", "host"):
        yield {"scope": scope, "attributes": explicit, "preferences": set(), "gamut": "srgb"}
        yield {
            "scope": scope, "attributes": explicit, "preferences": set(MEDIA.values()),
            "gamut": "srgb",
        }
        yield {"scope": scope, "attributes": attributes, "preferences": preferred, "gamut": "srgb"}
        for gamut in ("p3", "rec2020"):
            yield {"scope": scope, "attributes": explicit, "preferences": set(), "gamut": gamut}


def check_css(catalogue, text):
    problems = []
    rules = parse_css(text)
    expected_names = {
        token.css: token for token in catalogue.tokens if token.replacement is None
    }
    deprecated = {token.css for token in catalogue.tokens if token.replacement is not None}
    declared = {name for rule in rules for name, _ in rule["declarations"]}
    if declared - set(expected_names):
        problems.append(f"unexpected custom properties {sorted(declared - set(expected_names))[:5]}")
    if declared & deprecated:
        problems.append(f"deprecated aliases are declared: {sorted(declared & deprecated)}")
    for index in range(len(catalogue.selections)):
        for environment in environments(catalogue, index):
            properties = cascade(rules, environment)
            where = f"{context_key(catalogue, index)} {environment['scope']} {environment['gamut']}"
            if environment["preferences"]:
                where += " " + ",".join(sorted(environment["preferences"]))
            missing = set(expected_names) - set(properties)
            if missing:
                problems.append(f"{where}: missing {sorted(missing)[:5]}")
                continue
            for name, token in expected_names.items():
                try:
                    actual = css_value(token.kind, substitute(properties[name], properties))
                except ValueError as error:
                    problems.append(f"{where}: {name}: {error}")
                    continue
                expected = expected_css(token.kind, token.values[index], environment["gamut"])
                if actual != expected:
                    problems.append(f"{where}: {name} is {actual!r}, expected {expected!r}")
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Compare generated design-token outputs with values resolved from the sources")
    parser.add_argument("--root", type=pathlib.Path, default=ROOT)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--css", type=pathlib.Path, nargs="?", const=ROOT / CSS_PATH)
    group.add_argument("--dump", type=pathlib.Path)
    group.add_argument("--run", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    try:
        catalogue = load(args.root.resolve() / "design/tokens")
        if args.css is not None:
            label = "css"
            problems = check_css(catalogue, args.css.read_text(encoding="utf-8"))
        else:
            if args.run:
                output = subprocess.run(
                    args.run, check=True, capture_output=True, text=True).stdout
            else:
                output = args.dump.read_text(encoding="utf-8")
            document = json.loads(output)
            label = document.get("language", "dump")
            problems = check_dump(catalogue, document)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"golden: {error}", file=sys.stderr)
        return 1
    if problems:
        for problem in problems[:40]:
            print(f"golden {label}: {problem}", file=sys.stderr)
        print(f"golden {label}: {len(problems)} mismatches", file=sys.stderr)
        return 1
    count = len(catalogue.tokens if label == "css" else catalogue.public)
    print(
        f"golden {label}: {count} tokens match the Resolver in "
        f"{len(catalogue.selections)} contexts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
