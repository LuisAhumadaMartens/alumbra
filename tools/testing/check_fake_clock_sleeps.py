import argparse
import pathlib
import re
import sys

FAKE_CLOCK = re.compile(r"\b(FakeTickClock|TestTaskEnvironment)\b")
REAL_SLEEP = re.compile(
    r"\b(sleep_for|sleep_until|SleepFor|usleep|nanosleep|clock_nanosleep|Sleep|sleep)\s*\(")
INCLUDE = re.compile(r'^\s*#\s*include\s*["<]([^">]+)[">]', re.MULTILINE)
SUFFIXES = frozenset({".cc", ".cpp", ".cxx", ".h", ".hpp", ".hh", ".hxx", ".mm"})


def violations(roots, excluded):
    if isinstance(roots, pathlib.Path):
        roots = [roots]
    texts = {}
    for root in roots:
        for path in sorted(root.rglob("*")):
            if path.suffix not in SUFFIXES or not path.is_file():
                continue
            if any(path.is_relative_to(directory) for directory in excluded):
                continue
            texts[path] = path.read_text(encoding="utf-8", errors="replace")
    dependencies = {}
    for path, text in texts.items():
        dependencies[path] = set()
        for included in INCLUDE.findall(text):
            local = (path.parent / included).resolve()
            if local in texts:
                dependencies[path].add(local)
            else:
                dependencies[path].update(
                    candidate for candidate in texts
                    if candidate.as_posix().endswith("/" + included))

    def uses_fake_clock(path, visited):
        if path in visited:
            return False
        visited.add(path)
        return bool(FAKE_CLOCK.search(texts[path])) or any(
            uses_fake_clock(dependency, visited)
            for dependency in dependencies[path])

    for path, text in texts.items():
        if not uses_fake_clock(path, set()):
            continue
        for match in REAL_SLEEP.finditer(text):
            number = text.count("\n", 0, match.start()) + 1
            yield f"{path}:{number}: real sleep in a test that uses the fake clock"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("roots", nargs="+", type=pathlib.Path)
    parser.add_argument("--exclude", action="append", default=[],
                        type=pathlib.Path)
    args = parser.parse_args()
    excluded = [directory.resolve() for directory in args.exclude]
    scanned = [root.resolve() for root in args.roots]
    missing = [str(root) for root in scanned if not root.is_dir()]
    if missing:
        print(f"no such directory: {', '.join(missing)}", file=sys.stderr)
        return 2
    found = list(violations(scanned, excluded))
    for message in found:
        print(message, file=sys.stderr)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
