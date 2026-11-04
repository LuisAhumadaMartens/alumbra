import argparse
import pathlib
import subprocess


TARGETS = (
    "ass_parser_fuzzer",
    "srt_parser_fuzzer",
    "ttml_parser_fuzzer",
    "webvtt_fuzzer",
)


def run(command):
    return subprocess.run(command, check=True, capture_output=True,
                          text=True).stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=pathlib.Path)
    parser.add_argument("--profile-dir", required=True, type=pathlib.Path)
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--llvm-profdata", required=True)
    parser.add_argument("--llvm-cov", required=True)
    args = parser.parse_args()

    executables = {
        target: args.build_dir / "test" / "fuzz" / target
        for target in TARGETS
    }
    missing = [str(path) for path in executables.values() if not path.is_file()]
    if missing:
        parser.error("missing text fuzz targets: " + ", ".join(missing))

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for target, executable in executables.items():
        profiles = sorted((args.profile_dir / target).glob("*.profraw"))
        if not profiles:
            parser.error(
                f"no coverage profiles were written under "
                f"{args.profile_dir / target}")
        profile = args.output_dir / f"{target}.profdata"
        run([args.llvm_profdata, "merge", "-sparse", "-o", str(profile),
             *(str(path) for path in profiles)])
        report = run([args.llvm_cov, "report", "-instr-profile", str(profile),
                      str(executable)])
        details = run([args.llvm_cov, "show", "-instr-profile", str(profile),
                       "-show-functions", "-show-line-counts-or-regions",
                       str(executable)])
        (args.output_dir / f"{target}-summary.txt").write_text(
            report, encoding="utf-8")
        (args.output_dir / f"{target}.txt").write_text(
            details, encoding="utf-8")


if __name__ == "__main__":
    main()
