import pathlib
import re
import sys

USES = re.compile(r"^\s*(?:-\s+)?uses:\s*['\"]?([^'\"\s#]+)")
PINNED = re.compile(r"^[\w.-]+/[\w./-]+@[0-9a-f]{40}$")
DOCKER = re.compile(r"^docker://[^@]+@sha256:[0-9a-f]{64}$")


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".github")
    files = sorted(root.rglob("*.yml")) + sorted(root.rglob("*.yaml"))
    failures = []
    count = 0
    for path in files:
        for number, line in enumerate(path.read_text().splitlines(), 1):
            match = USES.match(line)
            if not match:
                continue
            count += 1
            target = match.group(1)
            if target.startswith("./") or PINNED.match(target) \
                    or DOCKER.match(target):
                continue
            failures.append(f"{path}:{number}: '{target}' is not pinned to a "
                            "full commit SHA")
    for failure in failures:
        print(failure, file=sys.stderr)
    if not count:
        print(f"No 'uses:' entries found under {root}", file=sys.stderr)
        return 1
    print(f"{count} 'uses:' entries checked, {len(failures)} unpinned")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
