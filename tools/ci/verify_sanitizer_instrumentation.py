import argparse
import json
import pathlib
import shutil
import subprocess
import sys


FLAGS = {
    "address": (
        "-fsanitize=address,undefined",
        "-fno-sanitize-recover=undefined",
        "-fno-omit-frame-pointer",
    ),
    "thread": ("-fsanitize=thread", "-fno-omit-frame-pointer"),
}
HOOKS = {
    "address": ("__asan_", "__ubsan_"),
    "thread": ("__tsan_",),
}
DEPENDENCY_ARCHIVES = {
    "Abseil": ("libabsl*.a",),
    "GoogleTest": ("libgtest.a",),
    "Google Benchmark": ("libbenchmark.a",),
}


def command_text(entry):
    command = entry.get("command")
    if command is not None:
        return command
    return " ".join(entry.get("arguments", []))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=pathlib.Path)
    parser.add_argument("--triplet", required=True)
    parser.add_argument("--sanitizer", choices=FLAGS, required=True)
    args = parser.parse_args()

    compile_commands = args.build_dir / "compile_commands.json"
    if not compile_commands.is_file():
        parser.error(f"missing compile database: {compile_commands}")
    commands = json.loads(compile_commands.read_text(encoding="utf-8"))
    if not commands:
        parser.error(f"compile database is empty: {compile_commands}")
    uninstrumented = [
        entry.get("file", "<unknown source>")
        for entry in commands
        if any(flag not in command_text(entry) for flag in FLAGS[args.sanitizer])
    ]
    if uninstrumented:
        parser.error(
            "Alumbra compile commands are missing sanitizer flags for: "
            + ", ".join(uninstrumented))

    install_dir = args.build_dir / "vcpkg_installed" / args.triplet
    library_dirs = (
        ("Debug", install_dir / "debug" / "lib"),
        ("Release", install_dir / "lib"),
    )
    nm = shutil.which("llvm-nm") or shutil.which("nm")
    if not nm:
        parser.error("neither llvm-nm nor nm is available")

    for configuration, library_dir in library_dirs:
        if not library_dir.is_dir():
            parser.error(
                f"missing vcpkg {configuration} library directory: "
                f"{library_dir}")
        for dependency, patterns in DEPENDENCY_ARCHIVES.items():
            archives = sorted({
                archive
                for pattern in patterns
                for archive in library_dir.glob(pattern)
            })
            if not archives:
                parser.error(
                    f"no {dependency} static archives in {library_dir}")
            instrumented = {hook: [] for hook in HOOKS[args.sanitizer]}
            for archive in archives:
                result = subprocess.run(
                    [nm, "-u", str(archive)], capture_output=True, text=True,
                    check=False)
                if result.returncode != 0:
                    parser.error(
                        f"could not inspect {archive}: "
                        f"{result.stderr.strip() or result.stdout.strip()}")
                symbols = result.stdout
                for hook in instrumented:
                    if hook in symbols:
                        instrumented[hook].append(archive.name)
            missing_hooks = [
                hook for hook, matches in instrumented.items() if not matches]
            if missing_hooks:
                parser.error(
                    f"{configuration} {dependency} archives are missing "
                    f"runtime hooks: {', '.join(missing_hooks)}")
            for hook, matches in instrumented.items():
                examples = ", ".join(matches[:3])
                more = len(matches) - 3
                if more > 0:
                    examples += f", and {more} more"
                print(
                    f"{configuration} {dependency}: {len(matches)}/"
                    f"{len(archives)} archives reference {hook} "
                    f"(for example {examples})")
    print(
        f"Verified {args.sanitizer} compile flags in Alumbra's compile "
        f"database and instrumented dependency archives for {args.triplet}.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"Sanitizer instrumentation verification failed: {error}",
              file=sys.stderr)
        raise SystemExit(1)
