import argparse
import pathlib
import re
import sys

FAKE_CLOCK = re.compile(r"\b(FakeTickClock|TestTaskEnvironment)\b")
REAL_SLEEP = re.compile(
    r"\b(sleep_for|sleep_until|SleepFor|usleep|nanosleep|Sleep)\s*\("
    r"|(?<![\w:])sleep\s*\(")
SUFFIXES = frozenset({".cc", ".cpp", ".h", ".hpp", ".mm"})


def violations(root, excluded):
    for path in sorted(root.rglob("*")):
        if path.suffix not in SUFFIXES or not path.is_file():
            continue
        if any(path.is_relative_to(directory) for directory in excluded):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if not FAKE_CLOCK.search(text):
            continue
        for number, line in enumerate(text.splitlines(), start=1):
            if REAL_SLEEP.search(line):
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
    found = [message for root in scanned
             for message in violations(root, excluded)]
    for message in found:
        print(message, file=sys.stderr)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
