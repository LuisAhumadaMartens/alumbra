import argparse
import pathlib
import sys

from token_cpp import render_cpp
from token_reference import render_reference
from token_languages import render_kotlin, render_swift, render_ts
from token_model import (
    COMMAND,
    CPP_PATH,
    CSS_PATH,
    DOCS_PATH,
    KOTLIN_PATH,
    MEDIA,
    REFERENCE_PATH,
    ROOT,
    SWIFT_PATH,
    TS_PATH,
    CssWriter,
    load,
    natural_key,
    number_text,
)


def hex_color(components):
    return "#" + "".join(f"{round(part * 255):02x}" for part in components)


def describe(kind, value):
    if kind == "color":
        text = hex_color(value["srgb"])
        if value["colorSpace"] != "srgb":
            parts = " ".join(number_text(part) for part in value["components"])
            text = f"color({value['colorSpace']} {parts}), sRGB fallback {text}"
        if value["alpha"] != 1:
            text += f" at {number_text(value['alpha'] * 100)}% alpha"
        return text
    if kind == "dimension":
        return f"{number_text(value)} px"
    if kind == "duration":
        return f"{number_text(value)} ms"
    if kind in {"number", "fontWeight"}:
        return number_text(value)
    if kind == "fontFamily":
        return ", ".join(value)
    if kind == "cubicBezier":
        return "cubic-bezier(" + ", ".join(number_text(part) for part in value) + ")"
    if kind == "shadow":
        return ", ".join(
            ("inset " if layer["inset"] else "")
            + " ".join(
                f"{number_text(layer[key])}px"
                for key in ("offsetX", "offsetY", "blur", "spread"))
            + " " + describe("color", layer["color"])
            for layer in value)
    return str(value)


def cell(text):
    return str(text).replace("|", "\\|").replace("\n", " ")


def render_docs(catalogue):
    lines = [
        "# Design token reference",
        "",
        f"Generated from [`design/tokens/`](../../design/tokens/) by `{COMMAND}`;",
        "do not edit it by hand. [Generated outputs](../../design/README.md#generated-outputs) explains the",
        "output files, the unit mapping and colour handling.",
        "",
        "## Contexts",
        "",
        "| Modifier | Contexts | Default | CSS attribute | Media query that selects a context |",
        "|----------|----------|---------|---------------|-------------------------------------|",
    ]
    for modifier in catalogue.modifiers:
        media = [
            f"`{query}` → `{context}`"
            for (name, context), query in MEDIA.items() if name == modifier.name
        ]
        lines.append(
            f"| `{modifier.name}` | {', '.join(f'`{context}`' for context in modifier.contexts)} "
            f"| `{modifier.default}` | `data-alumbra-{modifier.name}` | "
            f"{'; '.join(media) if media else 'none; set the attribute'} |")
    lines += [
        "",
        "## Tokens",
        "",
        "Default values are for the default context. Primitive tokens exist only as CSS custom properties, where",
        "aliases and contexts resolve them; renderers use semantic and component tokens.",
        "",
        "| Token | Tier | Type | CSS custom property | Identifier | Alias of | Varies with | Default | Description |",
        "|-------|------|------|---------------------|------------|----------|-------------|---------|-------------|",
    ]
    default = catalogue.default_index
    for token in sorted(catalogue.tokens, key=lambda item: natural_key(item.segments)):
        if not token.public:
            identifier, alias, varies = "CSS only", "", ""
        else:
            identifier = f"`{token.identifier}`"
            alias = f"`{token.alias}`" if token.alias else ""
            varies = (
                "alias" if token.alias
                else ", ".join(f"`{modifier.name}`" for modifier in token.modifiers) or "constant")
        lines.append(
            f"| `{token.name}` | {token.tier} | `{token.kind}` | `{token.css}` | {identifier} | "
            f"{alias} | {varies} | {cell(describe(token.kind, token.values[default]))} | "
            f"{cell(token.description)} |")
    return "\n".join(lines) + "\n"


def render(catalogue):
    return {
        CSS_PATH: CssWriter(catalogue).render(),
        TS_PATH: render_ts(catalogue),
        SWIFT_PATH: render_swift(catalogue),
        KOTLIN_PATH: render_kotlin(catalogue),
        CPP_PATH: render_cpp(catalogue),
        DOCS_PATH: render_docs(catalogue),
        REFERENCE_PATH: render_reference(catalogue, COMMAND),
    }


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate CSS, TypeScript, Swift, Kotlin and C++ design-token outputs")
    parser.add_argument("--check", action="store_true",
                        help="fail when a committed output differs from the generated one")
    parser.add_argument("--root", type=pathlib.Path, default=ROOT)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    try:
        outputs = render(load(root / "design/tokens"))
        stale = []
        for relative, content in outputs.items():
            path = root / relative
            if args.check:
                if not path.is_file() or path.read_bytes() != content.encode("utf-8"):
                    stale.append(relative)
                continue
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content.encode("utf-8"))
        if stale:
            raise ValueError(
                "stale generated token outputs: " + ", ".join(stale) + f"; run {COMMAND}")
    except (ValueError, OSError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
