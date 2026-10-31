import argparse
import fnmatch
import json
import os
import pathlib
import posixpath
import re
import sys

DEFAULT_ALLOWLIST = pathlib.Path(__file__).resolve().with_name("allowlist.json")
SOURCE_SUFFIXES = frozenset({
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inc", ".inl",
    ".ipp", ".m", ".mm",
})
DIRECTIVE = re.compile(r"^\s*#\s*(?:include_next|include|import)\b(.*)$")


def matches(name, entry):
    if name in entry.get("names", ()):
        return True
    return any(fnmatch.fnmatchcase(name, pattern)
               for pattern in entry.get("patterns", ()))


class Allowlist:
    def __init__(self, path, section):
        data = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
        data = data[section]
        self.entries = {}
        for entry in data["entries"]:
            if not str(entry.get("rationale", "")).strip():
                raise SystemExit(
                    f"{path}: {section} entry '{entry.get('id')}' has no rationale")
            self.entries[entry["id"]] = entry
        self.scopes = data["scopes"]
        for scope in self.scopes:
            for entry_id in scope["allow"]:
                if entry_id not in self.entries:
                    raise SystemExit(
                        f"{path}: {section} scope '{scope['name']}' allows "
                        f"unknown entry '{entry_id}'")
        self.ports = data["ports"]

    def allowed(self, scope, name):
        for entry_id in scope["allow"]:
            if matches(name, self.entries[entry_id]):
                return self.entries[entry_id]
        return None


def source_files(directory):
    if not directory.is_dir():
        return []
    return sorted(path for path in directory.rglob("*")
                  if path.is_file() and path.suffix in SOURCE_SUFFIXES)


def directives(path):
    with open(path, encoding="utf-8", errors="replace") as stream:
        for number, line in enumerate(stream, start=1):
            found = DIRECTIVE.match(line)
            if found:
                yield number, found.group(1).strip()


def parse_operand(operand):
    for opening, closing in (("<", ">"), ('"', '"')):
        if operand.startswith(opening):
            end = operand.find(closing, 1)
            if end > 1:
                return operand[1:end], operand[:end + 1], opening == '"'
    return None, operand, False


def logical_path(root, source, header, quoted):
    if quoted:
        candidate = source.parent / header
        if candidate.is_file():
            relative = os.path.relpath(candidate, root).replace(os.sep, "/")
            if relative != ".." and not relative.startswith("../"):
                return relative
    return posixpath.normpath(header)


def describe(display, header, logical):
    if header is not None and logical != posixpath.normpath(header):
        return f"{display} (resolves to {logical})"
    return display


def check_includes(arguments):
    allowlist = Allowlist(arguments.allowlist, "includes")
    root = arguments.root.resolve()
    violations = []
    checked = 0
    for scope in allowlist.scopes:
        for source in source_files(root / scope["root"]):
            checked += 1
            relative = source.relative_to(root).as_posix()
            for number, operand in directives(source):
                header, display, quoted = parse_operand(operand)
                if header is None:
                    violations.append(
                        f"{relative}:{number}: {scope['name']} may not use the "
                        f"computed include {display}")
                    continue
                logical = logical_path(root, source, header, quoted)
                if allowlist.allowed(scope, logical) is None:
                    violations.append(
                        f"{relative}:{number}: {scope['name']} may not include "
                        f"{describe(display, header, logical)}")
    ports_root = root / allowlist.ports["root"]
    shared = set(allowlist.ports["shared"])
    port_include = re.compile(
        rf"^{re.escape(allowlist.ports['root'])}/([^/]+)/")
    ports = sorted(path for path in ports_root.iterdir()
                   if path.is_dir() and path.name not in shared) \
        if ports_root.is_dir() else []
    for port in ports:
        for source in source_files(port):
            checked += 1
            relative = source.relative_to(root).as_posix()
            for number, operand in directives(source):
                header, display, quoted = parse_operand(operand)
                if header is None:
                    continue
                logical = logical_path(root, source, header, quoted)
                found = port_include.match(logical)
                if found and found.group(1) not in shared \
                        and found.group(1) != port.name:
                    violations.append(
                        f"{relative}:{number}: port {port.name} may not include "
                        f"{describe(display, header, logical)} from port "
                        f"{found.group(1)}")
    if checked == 0:
        violations.append(f"{root}: no layered source files found")
    return violations, f"layering includes: {checked} files checked"


def read_graph(path):
    names, links, aliases = {}, {}, {}
    for number, line in enumerate(
            pathlib.Path(path).read_text(encoding="utf-8").splitlines(), 1):
        fields = line.split("\t")
        if fields[0] == "node" and len(fields) == 3:
            names[fields[1]] = fields[2]
            links.setdefault(fields[1], [])
        elif fields[0] == "link" and len(fields) == 3:
            links.setdefault(fields[1], []).extend(
                item for item in fields[2].split(";")
                if item and not item.startswith("::@"))
        elif fields[0] == "alias" and len(fields) == 3:
            aliases[fields[1]] = fields[2]
        elif line:
            raise SystemExit(f"{path}:{number}: unrecognised graph record")
    return names, links, aliases


def check_links(arguments):
    allowlist = Allowlist(arguments.allowlist, "links")
    names, links, aliases = read_graph(arguments.graph)

    def resolve(item):
        target = aliases.get(item, item)
        return target, names.get(target, item)

    def walk(root, visit):
        stack = [(root, (names[root],))]
        visited = {root}
        while stack:
            current, path = stack.pop()
            for item in links.get(current, ()):
                child, name = resolve(item)
                if child == current:
                    continue
                if visit(path, name) and child in links \
                        and child not in visited:
                    visited.add(child)
                    stack.append((child, path + (name,)))

    violations = set()
    roots = 0
    for scope in allowlist.scopes:
        for target in sorted(names):
            if not any(fnmatch.fnmatchcase(names[target], pattern)
                       for pattern in scope["targets"]):
                continue
            roots += 1

            def visit_scope(path, name, scope=scope):
                entry = allowlist.allowed(scope, name)
                if entry is None:
                    violations.add(
                        f"{scope['name']}: edge {path[-1]} -> {name} is not "
                        f"allowed; path: {' -> '.join(path + (name,))}")
                    return False
                return bool(entry.get("traverse", False))

            walk(target, visit_scope)
    port_entry = {"patterns": allowlist.ports["targets"]}
    shared = set(allowlist.ports["shared"])

    def is_port(name):
        return matches(name, port_entry) and name not in shared

    for target in sorted(names):
        if not is_port(names[target]):
            continue
        roots += 1

        def visit_port(path, name, port=names[target]):
            if is_port(name) and name != port:
                violations.add(
                    f"ports: edge {path[-1]} -> {name} links port {name} from "
                    f"port {port}; path: {' -> '.join(path + (name,))}")
                return False
            return True

        walk(target, visit_port)
    if roots == 0:
        violations.add(f"{arguments.graph}: no layered targets found")
    return sorted(violations), \
        f"layering links: {len(names)} targets, {roots} layered roots checked"


def main():
    parser = argparse.ArgumentParser(
        description="Check Alumbra's layer dependency rules.")
    parser.add_argument("--allowlist", type=pathlib.Path,
                        default=DEFAULT_ALLOWLIST)
    commands = parser.add_subparsers(dest="command", required=True)
    includes = commands.add_parser("includes")
    includes.add_argument("--root", type=pathlib.Path, required=True)
    graph = commands.add_parser("links")
    graph.add_argument("--graph", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    check = check_includes if arguments.command == "includes" else check_links
    violations, summary = check(arguments)
    for violation in violations:
        print(violation, file=sys.stderr)
    if violations:
        print(f"layering {arguments.command}: {len(violations)} violation(s); "
              "see docs/architecture/layering.md", file=sys.stderr)
        return 1
    print(summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
