import argparse
import pathlib
import re
import shlex
import subprocess
import sys

from quality import ROOT, is_source, source_files, translation_units

EXEMPT = ("tools/cxx-probe/", "test/build/")


LINE_MARKER = re.compile(r'^#\s+(\d+)\s+"((?:\\.|[^"\\])*)"')


def base_command(entry, compiler):
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
    return result


def compatibility_command(entry, compiler):
    return [*base_command(entry, compiler), "-std=c++20", "-fsyntax-only",
            "-pedantic-errors", "-Werror=c++23-extensions",
            "-Werror=unknown-attributes", *resource_arguments()]


def preprocess_command(entry, compiler, standard):
    return [*base_command(entry, compiler), f"-std={standard}", "-E",
            *resource_arguments()]


def owned_path(name, directory):
    path = pathlib.Path(name)
    if not path.is_absolute():
        path = pathlib.Path(directory) / path
    try:
        relative = path.resolve().relative_to(ROOT).as_posix()
    except (OSError, ValueError):
        return None
    if not is_source(relative) or relative.startswith(EXEMPT):
        return None
    return relative


def owned_source_lines(preprocessed, directory):
    lines = set()
    current = None
    number = 0
    for text in preprocessed.splitlines():
        marker = LINE_MARKER.match(text)
        if marker:
            number = int(marker.group(1))
            current = owned_path(marker.group(2), directory)
            continue
        if current is not None and text.strip():
            lines.add((current, number))
        number += 1
    return lines


def version_dependent_lines(entry, compiler):
    def lines(standard):
        result = subprocess.run(
            preprocess_command(entry, compiler, standard),
            cwd=entry["directory"], capture_output=True, text=True)
        return owned_source_lines(result.stdout, entry["directory"])

    differences = {}
    for name, number in sorted(lines("c++20") ^ lines("c++23")):
        differences.setdefault(name, []).append(number)
    return differences


def resource_arguments():
    if sys.platform != "darwin":
        return []
    resource = subprocess.check_output(
        ["xcrun", "clang", "-print-resource-dir"], text=True).strip()
    return [f"-resource-dir={resource}"]


def source_violations(text):
    text = re.sub(r"\\\r?\n", " ", text)
    text = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', "", text,
                  flags=re.DOTALL)
    violations = []
    for pattern, name in [
        (r"(?:^|\n)\s*(?:export\s+)?(?:import|module)\s+[^;]+;",
         "C++ modules"),
        (r"\[\[\s*(?:assume|gnu::assume)\b", "[[assume]]"),
        (r"\bstd\s*::\s*(?:v?format(?:_to(?:_n)?)?|formatted_size)\b",
         "std::format (Apple-floor gated; use absl::StrFormat)"),
        (r"(?:^|\n)[ \t]*#[ \t]*"
         r"(?:if|elif|ifdef|ifndef|elifdef|elifndef|define)\b"
         r"[^\n]*\b(?:__cplusplus|__cpp_\w+)\b",
         "conditional compilation on the language version or feature-test "
         "macros (hides code from the C++20 replay)"),
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
        if name.startswith(EXEMPT):
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
        for name, numbers in version_dependent_lines(
                entry, args.compiler).items():
            shown = ", ".join(str(number) for number in numbers[:5])
            print(f"{name}: lines {shown} preprocess differently in C++20 and "
                  "C++23; version-dependent code hides from the C++20 replay",
                  flush=True)
            failed = True
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
