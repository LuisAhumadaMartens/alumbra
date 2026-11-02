import re
import sys
from pathlib import Path

import generate

ROOT = generate.ROOT
INDEX = "test/api_conformance/wpt-index.md"
PORTED = "test/api_conformance/wpt_media_elements_test.cc"
STATUSES = ("adopted", "adapted", "deviation", "not-applicable", "deferred")
WITH_TESTS = ("adopted", "adapted", "deviation")
ROW_PATTERN = re.compile(r"^\| `([^`]+)` \| ([\w-]+) \| (.*?) \| (.*) \|$")
MINIMUM_REASON = 20


def parse(text):
    rows = []
    for line in text.splitlines():
        found = ROW_PATTERN.match(line)
        if found is None:
            continue
        path, status, tests, reason = found.groups()
        rows.append({
            "path": path,
            "status": status,
            "tests": re.findall(r"`([^`]+)`", tests),
            "reason": "" if reason.strip() == "-" else reason.strip(),
        })
    return rows


def ported_tests(source):
    pattern = re.compile(r"^TEST(?:_F)?\s*\(\s*(Wpt\w+)\s*,\s*(\w+)\s*\)",
                         re.MULTILINE)
    return {f"{suite}.{name}" for suite, name in pattern.findall(source)}


def verify(rows, tests, ported):
    errors = []
    if not rows:
        errors.append("the index has no rows")
    seen = set()
    named = set()
    for row in rows:
        path, status = row["path"], row["status"]
        if path in seen:
            errors.append(f"{path}: listed twice")
        seen.add(path)
        if status not in STATUSES:
            errors.append(f"{path}: unknown status {status}")
            continue
        if status in WITH_TESTS and not row["tests"]:
            errors.append(f"{path}: {status} names no test")
        if status not in WITH_TESTS and row["tests"]:
            errors.append(f"{path}: {status} must not name a test")
        if status not in WITH_TESTS and len(row["reason"]) < MINIMUM_REASON:
            errors.append(f"{path}: {status} needs a justification")
        if status == "deviation":
            if not any("DISABLED_" in test for test in row["tests"]):
                errors.append(f"{path}: a deviation names its disabled test")
            if not re.search(r"#\d+", row["reason"]):
                errors.append(f"{path}: a deviation links its issue")
        for test in row["tests"]:
            named.add(test)
            if test not in tests:
                errors.append(f"{path}: test {test} does not exist")
    for test in sorted(ported - named):
        errors.append(f"ported test {test} is not in the index")
    for test in sorted(ported):
        if "DISABLED_" in test and not any(
                test in row["tests"] and row["status"] == "deviation"
                for row in rows):
            errors.append(f"disabled test {test} is not a deviation row")
    return errors


def main():
    try:
        rows = parse((ROOT / INDEX).read_text())
        source = (ROOT / PORTED).read_text()
        errors = verify(rows, generate.known_tests(ROOT / generate.TEST_ROOT),
                        ported_tests(source))
    except OSError as error:
        print(error, file=sys.stderr)
        return 1
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
