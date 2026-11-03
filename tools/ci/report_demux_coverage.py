import argparse
import pathlib
import subprocess


TARGETS = (
    "mp4_demuxer_fuzzer",
    "fmp4_fragment_fuzzer",
    "ts_demuxer_fuzzer",
    "ts_psi_fuzzer",
    "mkv_demuxer_fuzzer",
    "ebml_fuzzer",
)


def run(command):
    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=pathlib.Path)
    parser.add_argument("--profile-dir", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--llvm-profdata", required=True)
    parser.add_argument("--llvm-cov", required=True)
    args = parser.parse_args()

    profiles = sorted(args.profile_dir.glob("*.profraw"))
    if not profiles:
        parser.error(f"no coverage profiles were written under {args.profile_dir}")
    executables = [args.build_dir / "test" / "fuzz" / target
                   for target in TARGETS]
    missing = [str(path) for path in executables if not path.is_file()]
    if missing:
        parser.error("missing demux fuzz targets: " + ", ".join(missing))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    profile = args.output.with_suffix(".profdata")
    run([args.llvm_profdata, "merge", "-sparse", "-o", str(profile),
         *(str(path) for path in profiles)])
    objects = [str(executables[0])]
    for executable in executables[1:]:
        objects.extend(("-object", str(executable)))
    summary = run([args.llvm_cov, "report", "-instr-profile", str(profile),
                   *objects])
    details = run([args.llvm_cov, "show", "-instr-profile", str(profile),
                   "-show-functions", "-show-line-counts-or-regions",
                   *objects])
    args.output.write_text(details, encoding="utf-8")
    summary_path = args.output.with_name(
        args.output.stem + "-summary" + args.output.suffix)
    summary_path.write_text(summary, encoding="utf-8")


if __name__ == "__main__":
    main()
