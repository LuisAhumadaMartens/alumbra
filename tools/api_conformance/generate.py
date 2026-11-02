import argparse
from pathlib import Path
import re
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[2]
SOURCE = "umbra/player/conformance.toml"
HEADER = "umbra/player/include/umbra/player/player.h"
EVENTS = "umbra/player/include/umbra/player/events.h"
OUTPUT = "docs/reference/html-media-element-conformance.md"
TEST_ROOT = "test"
KINDS = ("attribute", "method", "event", "extension")
STATUSES = ("implemented", "absent")
STABILITIES = ("stable", "experimental")
FIELDS = {"name", "kind", "cpp", "status", "since", "stability", "tests",
          "deviations", "note"}
REPOSITORY = "https://github.com/LuisAhumadaMartens/alumbra"
DEVIATION_PATTERN = re.compile(r"^(\w+\.\w+): (.+) \(#(\d+)\)$")
CONFORMANCE_PREFIXES = ("Wpt", "TraceReplayTest", "EventTraceTest")
TEST_PATTERN = re.compile(r"^\s*TEST(?:_F|_P)?\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)",
                          re.MULTILINE)
SPECIAL = {"Player", "operator"}


def load(path):
    with Path(path).open("rb") as stream:
        source = tomllib.load(stream)
    if (set(source) != {"version", "row"} or type(source["version"]) is not int
            or source["version"] != 1):
        raise ValueError("conformance source requires version 1 and rows")
    rows = source["row"]
    if not isinstance(rows, list) or not rows:
        raise ValueError("conformance source must not be empty")
    seen = set()
    for row in rows:
        if not isinstance(row, dict) or not {"name", "kind", "status"} <= set(
                row) or not set(row) <= FIELDS:
            raise ValueError("row has missing or unknown fields")
        name, kind, status = row["name"], row["kind"], row["status"]
        if not isinstance(name, str) or not name:
            raise ValueError("row name must be a non-empty string")
        if kind not in KINDS or status not in STATUSES:
            raise ValueError(f"{name}: invalid kind or status")
        if (kind, name) in seen:
            raise ValueError(f"{name}: duplicate {kind} row")
        seen.add((kind, name))
        for field in ("cpp", "tests", "deviations"):
            value = row.get(field, [])
            if (not isinstance(value, list) or
                    not all(isinstance(item, str) and item for item in value)):
                raise ValueError(f"{name}: {field} must be a list of strings")
        if row.get("note") is not None and not isinstance(row["note"], str):
            raise ValueError(f"{name}: note must be a string")
        for deviation in row.get("deviations", []):
            if not DEVIATION_PATTERN.match(deviation):
                raise ValueError(f"{name}: a deviation reads 'Suite.Test: "
                                 "summary (#issue)'")
        if status == "implemented":
            if row.get("stability") not in STABILITIES:
                raise ValueError(f"{name}: stability must be stable or "
                                 "experimental")
            if not isinstance(row.get("since"), str) or not row["since"]:
                raise ValueError(f"{name}: an implemented row needs 'since'")
            if not row.get("tests"):
                raise ValueError(f"{name}: an implemented row names no "
                                 "covering test")
        elif (row.get("cpp") or row.get("tests") or row.get("since")
              or row.get("deviations")):
            raise ValueError(f"{name}: an absent row has no C++ member, "
                             "milestone or test")
        elif row.get("stability") is not None:
            raise ValueError(f"{name}: an absent row has no stability label")
    return rows


def declaration_name(statement):
    depth = 0
    for index, character in enumerate(statement):
        if character == "<":
            depth += 1
        elif character == ">":
            depth -= 1
        elif character == "(" and depth == 0:
            found = re.search(r"(~?\w+)\s*$", statement[:index])
            return found.group(1) if found else None
    return None


def public_members(header_text):
    body = header_text.split("class Player {", 1)[1]
    body = body.split(" private:", 1)[0]
    body = re.sub(r"//[^\n]*", "", body)
    while re.search(r"\{[^{}]*\}", body):
        body = re.sub(r"\{[^{}]*\}", ";", body)
    names = []
    for statement in body.split(";"):
        name = declaration_name(statement)
        if name is None or name.startswith("~") or name in SPECIAL:
            continue
        if name not in names:
            names.append(name)
    return names


def event_names(events_text):
    return re.findall(r'kName = "(\w+)"', events_text)


def known_tests(root):
    found = set()
    for path in Path(root).rglob("*.cc"):
        for suite, name in TEST_PATTERN.findall(path.read_text()):
            found.add(f"{suite}.{name}")
    return found


def camel(parts):
    return parts[0] + "".join(part.title() for part in parts[1:])


def binding_name(cpp):
    if cpp.startswith("set_"):
        cpp = cpp[4:]
    if "_" in cpp:
        return camel(cpp.split("_"))
    return cpp[0].lower() + cpp[1:]


def verify(rows, header_text, tests, events_text=None):
    errors = []
    if events_text is not None:
        documented = {row["name"] for row in rows
                      if row["kind"] in ("event", "extension")}
        names = event_names(events_text)
        for name in names:
            if name not in documented:
                errors.append(f"event {name} has no conformance row")
        for row in rows:
            if (row["kind"] == "event"
                    and row["status"] == "implemented"
                    and row["name"] not in names):
                errors.append(f"{row['name']}: implemented event row has "
                              "no event type")
    members = public_members(header_text)
    claimed = {}
    for row in rows:
        for member in row.get("cpp", []):
            claimed.setdefault(member, []).append(row["name"])
    for member in members:
        if member not in claimed:
            errors.append(f"public member {member} has no conformance row")
    for member in claimed:
        if member not in members:
            errors.append(f"row names {member}, which is not a public member")
    for row in rows:
        for test in row.get("tests", []):
            if test not in tests:
                errors.append(f"{row['name']}: covering test {test} does not "
                              "exist")
        for deviation in row.get("deviations", []):
            test = DEVIATION_PATTERN.match(deviation).group(1)
            if test not in tests:
                errors.append(f"{row['name']}: deviation test {test} does "
                              "not exist")
        if row["kind"] in ("attribute", "method") and row["status"] == (
                "implemented"):
            names = {binding_name(member) for member in row["cpp"]}
            if names != {row["name"]}:
                errors.append(f"{row['name']}: C++ names {row['cpp']} do not "
                              "map to the HTML name")
    return errors


def is_conformance_test(test):
    return test.startswith(CONFORMANCE_PREFIXES)


def coverage(rows):
    summary = {}
    for kind in ("attribute", "method", "event"):
        implemented = [row for row in rows if row["kind"] == kind
                       and row["status"] == "implemented"]
        covered = [row for row in implemented
                   if any(is_conformance_test(test)
                          for test in row["tests"])]
        deviating = [row for row in implemented if row.get("deviations")]
        summary[kind] = (len(covered), len(implemented), len(deviating))
    return summary


def deviation_note(deviation):
    test, summary, issue = DEVIATION_PATTERN.match(deviation).groups()
    return (f"Known deviation: {summary} "
            f"([#{issue}]({REPOSITORY}/issues/{issue}), "
            f"`{test}`).")


def render(rows):
    figures = coverage(rows)
    names = (("attribute", "attributes"), ("method", "methods"),
             ("event", "events"))
    coverage_lines = [
        f"- {covered} of {total} implemented {label} are covered by the "
        f"API conformance suite; {deviating} carry a known deviation."
        for (kind, label), (covered, total, deviating) in zip(
            names, (figures[kind] for kind, _ in names))]
    lines = [
        "# HTMLMediaElement conformance", "",
        f"Generated from `{SOURCE}` by `tools/api_conformance/generate.py`.",
        "Never edit this page by hand ([ADR-0022](../adr/0022-documentation-"
        "site-and-api-reference.md)).",
        "The generator fails when a public member of `Player` has no row, or "
        "when an implemented row names a test that does not exist, so the "
        "table cannot drift from the code ([ADR-0023](../adr/0023-public-api-"
        "surface-and-media-error.md)).", "",
        "The C++ names map mechanically to the bindings: `Play()` becomes "
        "`play()`, `current_time()` becomes `currentTime`, and "
        "`set_current_time()` becomes the setter of that property. A `stable` "
        "member is not expected to change before 1.0; an `experimental` "
        "member may change in any release.", "",
        "## Conformance coverage", "",
        "The API conformance suite (`test/api_conformance`) ports web-platform-"
        "tests media cases and replays event traces against the fake port. "
        "Its WPT index is `test/api_conformance/wpt-index.md`.", "",
        *coverage_lines, "",
        "A known deviation is a web-platform-tests case that Alumbra fails "
        "today. Its test is disabled, named in the Notes column and tracked "
        "by an issue.", ""]
    sections = (("attribute", "Attributes"), ("method", "Methods"),
                ("event", "Events"), ("extension", "Extensions"))
    for kind, title in sections:
        selected = [row for row in rows if row["kind"] == kind]
        lines += [f"## {title}", "",
                  "| Name | C++ | Binding | Status | Stability | Tests | "
                  "Notes |",
                  "|------|-----|---------|--------|-----------|-------|"
                  "-------|"]
        for row in selected:
            cpp = ", ".join(f"`{item}`" for item in row.get("cpp", []))
            binding = ""
            if row["status"] == "implemented" and row.get("cpp"):
                binding = ", ".join(
                    f"`{item}`" for item in dict.fromkeys(
                        binding_name(member) for member in row["cpp"]))
            status = ("implemented since " + row["since"]
                      if row["status"] == "implemented" else "absent")
            tests = "<br>".join(f"`{item}`" for item in row.get("tests", []))
            notes = " ".join(
                ([row["note"]] if "note" in row else [])
                + [deviation_note(item) for item in row.get("deviations", [])])
            lines.append(
                f"| `{row['name']}` | {cpp or '-'} | {binding or '-'} | "
                f"{status} | {row.get('stability', '-')} | {tests or '-'} | "
                f"{notes} |")
        lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        rows = load(ROOT / SOURCE)
        errors = verify(rows, (ROOT / HEADER).read_text(),
                        known_tests(ROOT / TEST_ROOT),
                        (ROOT / EVENTS).read_text())
        if errors:
            raise ValueError("\n".join(errors))
        content = render(rows)
        path = ROOT / OUTPUT
        if args.check:
            if not path.is_file() or path.read_text() != content:
                raise ValueError(f"stale generated file: {OUTPUT}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
    except (ValueError, OSError, IndexError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
