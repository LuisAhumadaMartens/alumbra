import argparse
import collections
import dataclasses
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
from pathlib import Path

import doxygen
from sidecar_filter import ENTRY

ROOT = Path(__file__).resolve().parents[2]
TOOL = Path(__file__).resolve().parent
DOXYFILE = TOOL / "Doxyfile"
DEFAULT_OUTPUT = ROOT / "build/api/cpp"
THEME_LICENSE = TOOL / "third_party/doxygen-awesome-css/LICENSE"
HEADER_SUFFIXES = (".h", ".hh", ".hpp", ".hxx")
COMPOUND_KINDS = ("namespace", "class", "struct", "union")
MEMBER_KINDS = ("fn", "var", "enum", "typedef")
FORBIDDEN_COMMANDS = re.compile(
    r"[\\@](code|endcode|snippet\w*|dontinclude|skip|skipline|until|line|"
    r"verbatim|endverbatim)\b")
FENCE = re.compile(r"\s*(```|~~~)")
INDENTED = re.compile(r"( {4}|\t)")
LEADING_TEMPLATE = re.compile(r"^\s*template\s*<")
OPERATOR = (r"operator\s*(?:<=>|\(\)|\[\]|->\*?|<<=|>>=|<<|>>|\+\+|--|&&|\|\||"
            r"[-+*/%^&|~!<>=]=?)")
FUNCTION_NAME = re.compile(
    rf"((?:[A-Za-z_]\w*::)*(?:{OPERATOR}|~?[A-Za-z_]\w*))\s*\(")


@dataclasses.dataclass(frozen=True)
class Inputs:
    headers: tuple
    sidecars: Path
    examples: Path
    excluded_symbols: str


@dataclasses.dataclass(frozen=True)
class Entry:
    kind: str
    name: str
    path: Path
    line: int


def read_config(path):
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = shlex.split(value)
    return values


def repository_inputs():
    config = read_config(DOXYFILE)
    sidecars = ROOT / "docs/api/cpp"
    headers = tuple(ROOT / item for item in config["INPUT"]
                    if (ROOT / item).resolve() != sidecars.resolve())
    return Inputs(headers=headers, sidecars=sidecars,
                  examples=ROOT / config["EXAMPLE_PATH"][0],
                  excluded_symbols=" ".join(config["EXCLUDE_SYMBOLS"]))


def display(path):
    path = Path(path)
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def sidecar_files(directory):
    return sorted(path for path in Path(directory).rglob("*.md")
                  if path.is_file())


def header_files(directory):
    directory = Path(directory)
    return sorted(path for path in directory.rglob("*")
                  if path.is_file() and path.suffix in HEADER_SUFFIXES)


def lint(path):
    errors = []
    text = path.read_text(encoding="utf-8")
    where = display(path)
    if "*/" in text:
        errors.append(f"{where}: '*/' is not allowed in a sidecar")
    lines = text.splitlines()
    first = next(((number, line) for number, line in enumerate(lines, start=1)
                  if line.strip()), None)
    if first is None or not ENTRY.match(first[1]):
        line = first[0] if first else 1
        errors.append(f"{where}:{line}: a sidecar must start with a structural "
                      "command such as \\file or \\mainpage")
    for number, line in enumerate(lines, start=1):
        if FENCE.match(line):
            errors.append(f"{where}:{number}: code blocks are not allowed; "
                          "\\include a compiled example instead")
        elif INDENTED.match(line):
            errors.append(f"{where}:{number}: lines indented by four or more "
                          "spaces render as code; indent by at most three")
        command = FORBIDDEN_COMMANDS.search(line)
        if command:
            errors.append(f"{where}:{number}: \\{command.group(1)} is not "
                          "allowed; \\include a whole compiled example")
        entry = ENTRY.search(line)
        if entry and entry.start() != 0:
            errors.append(f"{where}:{number}: structural commands must start "
                          "a line")
    return errors


def strip_template(signature):
    if not LEADING_TEMPLATE.match(signature):
        return signature
    depth = 0
    for index, character in enumerate(signature):
        if character == "<":
            depth += 1
        elif character == ">":
            depth -= 1
            if depth == 0:
                return signature[index + 1:]
    return signature


def entry_name(kind, argument):
    argument = argument.strip()
    if kind == "fn":
        found = FUNCTION_NAME.search(strip_template(argument))
        if not found:
            return None
        return re.sub(r"\s+", "", found.group(1))
    tokens = argument.split()
    if not tokens:
        return None
    return tokens[-1] if kind in ("var", "typedef") else tokens[0]


def entries(path):
    found = []
    for number, line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), start=1):
        match = re.match(r"[\\@](\w+)\b(.*)", line)
        if not match or not ENTRY.match(line):
            continue
        kind, argument = match.groups()
        if kind in ("mainpage", "page"):
            continue
        found.append(Entry(kind, entry_name(kind, argument), path, number))
    return found


def text_of(element):
    return "" if element is None else "".join(element.itertext()).strip()


def documented(element):
    return bool(text_of(element.find("briefdescription"))
                or text_of(element.find("detaileddescription")))


def header_location(inputs, relative, line=None):
    for directory in inputs.headers:
        if (Path(directory) / relative).is_file():
            relative = display(Path(directory) / relative)
            break
    return relative if line is None else f"{relative}:{line}"


def read_xml(xml_dir, inputs):
    sidecars = {path.relative_to(inputs.sidecars).as_posix()
                for path in sidecar_files(inputs.sidecars)}
    errors = []
    documented_files = set()
    member_counts = collections.Counter()
    seen = set()
    for path in sorted(Path(xml_dir).glob("*.xml")):
        if path.name == "index.xml":
            continue
        for compound in ElementTree.parse(path).getroot().iter("compounddef"):
            kind = compound.get("kind")
            name = compound.findtext("compoundname")
            location = compound.find("location")
            located = "" if location is None else location.get("file", "")
            if kind == "file":
                if located not in sidecars and documented(compound):
                    documented_files.add(located)
                continue
            if kind not in COMPOUND_KINDS:
                continue
            if located in sidecars:
                where = f"{display(inputs.sidecars / located)}:{location.get('line')}"
                errors.append(f"{where}: \\{kind} {name} matches no "
                              "declaration in the public headers")
                continue
            if kind == "namespace" and not documented(compound):
                errors.append(f"{header_location(inputs, located)}: namespace "
                              f"{name} is not documented; add a \\namespace entry")
            for member in compound.iter("memberdef"):
                if member.get("prot") == "private" or member.get("id") in seen:
                    continue
                seen.add(member.get("id"))
                qualified = f"{name}::{member.findtext('name')}"
                member_counts[qualified] += documented(member)
                where = member.find("location")
                where = header_location(inputs, where.get("file"),
                                        where.get("line"))
                for value in member.iter("enumvalue"):
                    scope = qualified if member.get("strong") == "yes" else name
                    value_name = f"{scope}::{value.findtext('name')}"
                    if documented(value):
                        member_counts[value_name] += 1
                    else:
                        errors.append(f"{where}: enumerator {value_name} is "
                                      "not documented; add a \\var entry")
    return errors, documented_files, member_counts


def check_headers(inputs, documented_files):
    return [f"{display(header)}: no \\file entry documents this public header"
            for directory in inputs.headers
            for header in header_files(directory)
            if header.relative_to(directory).as_posix() not in documented_files]


def check_entries(sidecar_entries, member_counts):
    errors = []
    counts = collections.Counter((entry.kind, entry.name)
                                 for entry in sidecar_entries)
    reported = set()
    for entry in sidecar_entries:
        where = f"{display(entry.path)}:{entry.line}"
        key = (entry.kind, entry.name)
        if entry.name is None:
            errors.append(f"{where}: cannot read the symbol name of this "
                          f"\\{entry.kind} entry")
        elif key in reported:
            continue
        elif entry.kind in MEMBER_KINDS:
            declared = member_counts[entry.name]
            if counts[key] > declared:
                reported.add(key)
                errors.append(
                    f"{where}: {counts[key]} \\{entry.kind} entries name "
                    f"{entry.name}, which has {declared} documented "
                    "declaration(s); remove stale or duplicate entries")
        elif entry.kind not in MEMBER_KINDS and counts[key] > 1:
            reported.add(key)
            errors.append(f"{where}: \\{entry.kind} {entry.name} is documented "
                          "more than once")
    return errors


def check_xml(xml_dir, inputs, sidecar_entries):
    errors, documented_files, member_counts = read_xml(xml_dir, inputs)
    return (errors + check_headers(inputs, documented_files)
            + check_entries(sidecar_entries, member_counts))


def override(key, *values):
    quoted = " ".join('"' + str(value).replace('"', '\\"') + '"'
                      for value in values)
    return f"{key} = {quoted}\n"


def configuration(inputs, output):
    text = DOXYFILE.read_text(encoding="utf-8")
    text += override("INPUT", *inputs.headers, inputs.sidecars)
    text += override("STRIP_FROM_PATH", *inputs.headers, inputs.sidecars)
    text += override("STRIP_FROM_INC_PATH", *inputs.headers)
    text += override("EXAMPLE_PATH", inputs.examples)
    text += f"EXCLUDE_SYMBOLS = {inputs.excluded_symbols}\n"
    text += override("OUTPUT_DIRECTORY", output)
    text += override("WARN_LOGFILE", output / "warnings.log")
    return text


def find_doxygen(explicit=None):
    candidate = explicit or os.environ.get("DOXYGEN") or shutil.which("doxygen")
    if not candidate:
        raise RuntimeError(
            f"Doxygen {doxygen.VERSION} is required. Install it with "
            "python3 tools/cpp-reference/doxygen.py --install-dir "
            "build/tools/doxygen and pass the printed path with --doxygen.")
    version = subprocess.run([candidate, "--version"], capture_output=True,
                             text=True, check=True).stdout.strip()
    if version.split()[0] != doxygen.VERSION:
        raise RuntimeError(f"{candidate} is Doxygen {version}; the reference "
                           f"is pinned to Doxygen {doxygen.VERSION}")
    return candidate


def run_doxygen(executable, inputs, work):
    result = subprocess.run(
        [executable, "-"], input=configuration(inputs, work), text=True,
        cwd=ROOT, capture_output=True, check=False)
    log = work / "warnings.log"
    warnings = log.read_text(encoding="utf-8") if log.is_file() else ""
    root = str(ROOT) + os.sep
    lines = [line.replace(root, "") for line in
             (warnings + result.stderr).splitlines() if line.strip()]
    if result.returncode != 0 and not lines:
        lines.append(f"Doxygen exited with status {result.returncode}")
    return lines


def build(inputs, executable, work):
    errors = []
    for path in sidecar_files(inputs.sidecars):
        errors.extend(lint(path))
    if errors:
        return errors
    errors = run_doxygen(executable, inputs, work)
    if not (work / "xml").is_dir():
        return errors or ["Doxygen produced no XML output"]
    sidecar_entries = [entry for path in sidecar_files(inputs.sidecars)
                       for entry in entries(path)]
    return errors + check_xml(work / "xml", inputs, sidecar_entries)


def publish(html, output):
    output = Path(output)
    if output.exists():
        previous = (output / "index.html").is_file() and (
            output / "doxygen.css").is_file()
        if not output.is_dir() or (any(output.iterdir()) and not previous):
            raise RuntimeError(
                f"{output} exists and is not an earlier reference build; "
                "choose an empty or new output directory")
        shutil.rmtree(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(html, output)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Build the C++ API reference and enforce its coverage")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT,
                        help="directory that receives the HTML reference")
    parser.add_argument("--doxygen", help="path to the pinned Doxygen binary")
    args = parser.parse_args(argv)
    try:
        executable = find_doxygen(args.doxygen)
        with tempfile.TemporaryDirectory() as work:
            errors = build(repository_inputs(), executable, Path(work))
            if errors:
                print("\n".join(errors), file=sys.stderr)
                print(f"The C++ API reference has {len(errors)} problem(s); "
                      "see CONTRIBUTING.md#c-api-reference.", file=sys.stderr)
                return 1
            html = Path(work) / "html"
            shutil.copyfile(THEME_LICENSE,
                            html / "LICENSE-doxygen-awesome-css.txt")
            publish(html, args.output)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1
    print(f"C++ API reference written to {display(args.output)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
