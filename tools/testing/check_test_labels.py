import argparse
import json
import subprocess
import sys

KINDS = ("unit", "integration", "conformance", "golden", "benchmark", "fuzz",
         "sanitizer_canary")
ISSUE_PROPERTY = "ALUMBRA_QUARANTINE_ISSUE"


def listed_tests(ctest, test_dir, config, extra=()):
    command = [ctest, "--test-dir", test_dir, "--show-only=json-v1", *extra]
    if config:
        command += ["-C", config]
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"{' '.join(command)} failed: {result.stderr.strip()}")
    tests = {}
    for test in json.loads(result.stdout)["tests"]:
        properties = {p["name"]: p["value"] for p in test.get("properties", [])}
        tests[test["name"]] = properties
    return tests


def labels_of(properties):
    return list(properties.get("LABELS", []))


def kind_problems(tests):
    problems = []
    for name, properties in sorted(tests.items()):
        kinds = [label for label in labels_of(properties) if label in KINDS]
        if len(kinds) != 1:
            problems.append(f"{name} has kind labels {kinds}, expected exactly one of {list(KINDS)}")
    return problems


def quarantine_problems(tests):
    problems = []
    for name, properties in sorted(tests.items()):
        if "quarantine" in labels_of(properties) and not properties.get(ISSUE_PROPERTY):
            problems.append(f"{name} is quarantined without a linked issue")
    return problems


def selection_problems(all_tests, selected, kind):
    expected = {name for name, properties in all_tests.items()
                if kind in labels_of(properties)}
    problems = []
    for name in sorted(set(selected) - expected):
        problems.append(f"-L ^{kind}$ selected {name}, which lacks the {kind} label")
    for name in sorted(expected - set(selected)):
        problems.append(f"-L ^{kind}$ skipped {name}, which has the {kind} label")
    for name in sorted(selected):
        kinds = [label for label in labels_of(all_tests.get(name, {})) if label in KINDS]
        if name in expected and kinds != [kind]:
            problems.append(f"-L ^{kind}$ selected {name}, which is not only a {kind} test")
    if not selected:
        problems.append(f"-L ^{kind}$ selected no tests")
    return problems


def summary(tests):
    counts = {kind: 0 for kind in KINDS}
    for properties in tests.values():
        for label in labels_of(properties):
            if label in counts:
                counts[label] += 1
    lines = ["### Test labels", "", "| Kind | Tests |", "|------|-------|"]
    lines += [f"| `{kind}` | {count} |" for kind, count in counts.items()]
    quarantined = sorted((name, properties.get(ISSUE_PROPERTY, "?"))
                         for name, properties in tests.items()
                         if "quarantine" in labels_of(properties))
    lines += ["", f"**Quarantined tests: {len(quarantined)}**", ""]
    if quarantined:
        lines += [f"- `{name}`: issue #{issue}" for name, issue in quarantined]
    else:
        lines.append("None.")
    return "\n".join(lines) + "\n"


def run(arguments, out=sys.stdout):
    all_tests = listed_tests(arguments.ctest, arguments.test_dir, arguments.config)
    unit_tests = listed_tests(arguments.ctest, arguments.test_dir, arguments.config,
                              ("-L", "^unit$"))
    problems = (kind_problems(all_tests) + quarantine_problems(all_tests)
                + selection_problems(all_tests, unit_tests, "unit"))
    out.write(summary(all_tests))
    for problem in problems:
        print(problem, file=sys.stderr)
    return 1 if problems else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Check CTest kind labels and quarantine")
    parser.add_argument("--ctest", default="ctest")
    parser.add_argument("--test-dir", required=True)
    parser.add_argument("--config", default="")
    return run(parser.parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())
