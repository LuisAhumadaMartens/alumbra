import argparse
import os
import pathlib
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--diagnostic", required=True)
    args = parser.parse_args()

    try:
        result = subprocess.run(
            [args.executable, args.case], capture_output=True, text=True,
            timeout=30, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f"Could not run sanitizer canary {args.case}: {error}",
              file=sys.stderr)
        return 1

    output = result.stdout + result.stderr
    report_dir = os.environ.get("ALUMBRA_CANARY_REPORT_DIR")
    if report_dir:
        report_path = pathlib.Path(report_dir) / f"{args.case}.log"
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(output, encoding="utf-8")

    expected = re.compile(args.diagnostic)
    if result.returncode == 0:
        print(f"Sanitizer canary {args.case} unexpectedly succeeded.",
              file=sys.stderr)
        return 1
    if not expected.search(output):
        print(
            f"Sanitizer canary {args.case} failed without the expected "
            f"diagnostic {args.diagnostic!r} (exit {result.returncode}).",
            file=sys.stderr)
        print(output, file=sys.stderr)
        return 1

    print(f"Sanitizer canary {args.case} produced the expected diagnostic.")
    if report_dir:
        print(f"Evidence: {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
