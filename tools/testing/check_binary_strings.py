import argparse
import pathlib
import sys

CATEGORIES = [
    "net", "manifest", "abr", "buffer", "demux", "decode", "render", "audio",
    "text", "ads", "drm", "settings", "player", "port",
]


def c_strings(data):
    return {item for item in data.split(b"\0") if item}


def check(path, absent, present):
    strings = c_strings(pathlib.Path(path).read_bytes())
    failures = []
    for text in absent:
        if text.encode() in strings:
            failures.append(f"{path}: unexpected string {text!r}")
    for text in present:
        if text.encode() not in strings:
            failures.append(f"{path}: missing string {text!r}")
    return failures


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Check which exact C strings a binary contains")
    parser.add_argument("binary")
    parser.add_argument("--absent", action="append", default=[])
    parser.add_argument("--absent-categories", action="store_true")
    parser.add_argument("--present", action="append", default=[])
    args = parser.parse_args(argv)
    absent = list(args.absent)
    if args.absent_categories:
        absent += CATEGORIES
    failures = check(args.binary, absent, args.present)
    for failure in failures:
        print(failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
