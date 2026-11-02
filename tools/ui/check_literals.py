import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
STYLE_FILES = ("layout.cc", "compose.cc", "style.cc", "animation.cc")
STYLE_DIRECTORY = "antumbra/desktop_ui"
ALLOWED_NUMBERS = {"0", "1", "2", "3", "0.0", "0.5", "1.0", "2.0"}
FORBIDDEN_IDENTIFIERS = ("Srgb", "Srgba", "SDL_FColor", "SDL_Color")

CHARACTER = re.compile(r"'(?:\\.|[^'\\])'")
STRING = re.compile(r'"(?:\\.|[^"\\])*"')
HEX_COLOUR_STRING = re.compile(r'"#[0-9a-fA-F]{3,8}"')
HEX_NUMBER = re.compile(r"\b0[xX][0-9a-fA-F]+\b")
NUMBER = re.compile(
    r"(?<![\w.])(\d[\d']*\.\d*|\.\d+|\d[\d']*)(?:[eE][+-]?\d+)?[fFuUlL]*(?![\w.])")
COLOUR_CONSTRUCTION = re.compile(r"\bRgba\s*\{[^}]*\d")


def normalise(number):
    return number.replace("'", "").rstrip("0").rstrip(".") if "." in number else number


def violations_in(text, name):
    found = []
    for line_number, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if stripped.startswith("#include") or stripped.startswith("#"):
            continue
        if HEX_COLOUR_STRING.search(line):
            found.append(f"{name}:{line_number}: colour literal {stripped}")
        code = CHARACTER.sub("' '", STRING.sub('""', line))
        if HEX_NUMBER.search(code):
            found.append(f"{name}:{line_number}: hexadecimal literal {stripped}")
            continue
        if COLOUR_CONSTRUCTION.search(code):
            found.append(f"{name}:{line_number}: colour built from literals {stripped}")
        for identifier in FORBIDDEN_IDENTIFIERS:
            if re.search(rf"\b{identifier}\b", code):
                found.append(f"{name}:{line_number}: {identifier} is not a token {stripped}")
        for match in NUMBER.finditer(code):
            value = match.group(1)
            canonical = normalise(value) if "." in value else value
            if canonical in ALLOWED_NUMBERS or value in ALLOWED_NUMBERS:
                continue
            found.append(f"{name}:{line_number}: literal {value} in renderer code {stripped}")
    return found


def check(root):
    problems = []
    directory = root / STYLE_DIRECTORY
    for name in STYLE_FILES:
        path = directory / name
        if not path.is_file():
            problems.append(f"{STYLE_DIRECTORY}/{name}: missing")
            continue
        problems.extend(violations_in(path.read_text(), f"{STYLE_DIRECTORY}/{name}"))
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Reject literal colours and sizes in the desktop UI renderer's style code.")
    parser.add_argument("--root", type=pathlib.Path, default=ROOT)
    args = parser.parse_args(argv)
    problems = check(args.root)
    for problem in problems:
        print(problem, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
