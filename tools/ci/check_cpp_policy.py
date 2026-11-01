import argparse
import pathlib
import re
import shlex
import subprocess
import sys

from quality import ROOT, source_files, translation_units


def compatibility_command(entry, compiler):
    command = entry.get("arguments") or shlex.split(entry["command"])
    result = [compiler]
    skip = False
    for arg in command[1:]:
        if skip:
            skip = False
        elif arg in {"-o", "-MF", "-MT", "-MQ"}:
            skip = True
        elif arg not in {"-c", "-MD", "-MMD"} and not arg.startswith("-std="):
            result.append(arg)
    return [*result, "-std=c++20", "-fsyntax-only", "-pedantic-errors",
            "-Werror=c++23-extensions", "-Werror=unknown-attributes",
            *resource_arguments()]


def resource_arguments():
    if sys.platform != "darwin":
        return []
    resource = subprocess.check_output(
        ["xcrun", "clang", "-print-resource-dir"], text=True).strip()
    return [f"-resource-dir={resource}"]


def source_violations(text):
    text = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', "", text,
                  flags=re.DOTALL)
    violations = []
    for pattern, name in [
        (r"(?:^|\n)\s*(?:export\s+)?(?:import|module)\s+[^;]+;",
         "C++ modules"),
        (r"\[\[\s*(?:assume|gnu::assume)\b", "[[assume]]"),
        (r"\bstd\s*::\s*(?:v?format(?:_to(?:_n)?)?|formatted_size)\b",
         "std::format (Apple-floor gated; use absl::StrFormat)"),
    ]:
        for match in re.finditer(pattern, text):
            violations.append(name)
    return violations


def main():
    parser = argparse.ArgumentParser(description="Enforce ADR-0013 fail-closed")
    parser.add_argument("-p", type=pathlib.Path, required=True)
    parser.add_argument("--compiler", required=True)
    args = parser.parse_args()
    policy = (ROOT / "docs/cpp-features.md").read_text()
    table = policy.split("## Probed features\n", 1)[1].split("\n## ", 1)[0]
    if re.search(r"\|\s*allowed\s*\|", table):
        raise RuntimeError(
            "Allowing a feature requires updating the compatibility gate "
            "with its reviewed, exact-toolchain probe evidence")
    failed = False
    for name in source_files():
        if name.startswith(("tools/cxx-probe/", "test/build/")):
            continue
        for violation in source_violations((ROOT / name).read_text()):
            print(f"{name}: prohibited feature: {violation}", flush=True)
            failed = True
    seen = set()
    for file, entry in translation_units(args.p.resolve()):
        if file in seen:
            continue
        seen.add(file)
        print(f"C++20 compatibility: {file.relative_to(ROOT)}", flush=True)
        result = subprocess.run(
            compatibility_command(entry, args.compiler), cwd=entry["directory"])
        failed |= result.returncode != 0
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
