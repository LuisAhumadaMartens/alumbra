import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import time

FINDINGS = ("crash", "leak", "oom", "timeout")
SIGNATURES = (
    ("leak", re.compile(r"ERROR: LeakSanitizer: detected memory leaks")),
    ("oom", re.compile(r"ERROR: libFuzzer: out-of-memory")),
    ("timeout", re.compile(r"ERROR: libFuzzer: timeout after")),
    ("crash", re.compile(
        r"ERROR: (?:libFuzzer|AddressSanitizer|UndefinedBehaviorSanitizer)"
        r"|runtime error: ")),
)
STAT = re.compile(r"^stat::(\w+):\s+(\d+)\s*$", re.MULTILINE)
LOG = "libfuzzer.log"
RESULT = "result.json"


def positive(text):
    try:
        value = int(text)
    except ValueError:
        value = 0
    if value < 1:
        raise argparse.ArgumentTypeError(f"{text!r} is not a positive integer")
    return value


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        description="Run one libFuzzer target with a fixed smoke budget.")
    parser.add_argument("--name", required=True)
    parser.add_argument("--executable", required=True, type=pathlib.Path)
    parser.add_argument("--corpus", action="append", required=True,
                        type=pathlib.Path)
    parser.add_argument("--dict", type=pathlib.Path)
    parser.add_argument("--work-dir", required=True, type=pathlib.Path)
    parser.add_argument("--runs", required=True, type=positive)
    parser.add_argument("--seed", required=True, type=positive)
    parser.add_argument("--rss-limit-mb", required=True, type=positive)
    parser.add_argument("--timeout", required=True, type=positive)
    parser.add_argument("--expect", choices=FINDINGS)
    return parser.parse_args(argv)


def report_root(work_dir):
    configured = os.environ.get("ALUMBRA_FUZZ_REPORT_DIR")
    return pathlib.Path(configured) if configured else work_dir / "reports"


def seed_count(corpora):
    count = 0
    for corpus in corpora:
        if not corpus.is_dir():
            raise ValueError(f"corpus directory {corpus} does not exist")
        count += sum(1 for path in corpus.rglob("*") if path.is_file())
    if not count:
        raise ValueError("the seed corpus is empty")
    return count


def fresh_directory(path):
    shutil.rmtree(path, ignore_errors=True)
    path.mkdir(parents=True)
    return path


def libfuzzer_command(args, work_corpus, artifacts):
    command = [
        str(args.executable.resolve()),
        f"-runs={args.runs}",
        f"-seed={args.seed}",
        f"-rss_limit_mb={args.rss_limit_mb}",
        f"-timeout={args.timeout}",
        "-print_final_stats=1",
        f"-artifact_prefix={artifacts.resolve()}{os.sep}",
    ]
    if args.dict:
        command.append(f"-dict={args.dict.resolve()}")
    command.append(str(work_corpus.resolve()))
    command.extend(str(corpus.resolve()) for corpus in args.corpus)
    return command


def classify(output, artifacts):
    for kind in FINDINGS:
        if any(path.name.startswith(f"{kind}-") for path in artifacts):
            return kind
    for kind, pattern in SIGNATURES:
        if pattern.search(output):
            return kind
    return None


def run(args):
    reports = fresh_directory(report_root(args.work_dir) / args.name)
    result = {
        "name": args.name,
        "budget": {"runs": args.runs, "seed": args.seed,
                   "rss_limit_mb": args.rss_limit_mb,
                   "timeout": args.timeout},
        "expect": args.expect,
        "exit_code": None,
        "finding": None,
        "seconds": 0.0,
        "stats": {},
        "artifacts": [],
        "error": None,
    }
    try:
        result["seeds"] = seed_count(args.corpus)
    except ValueError as error:
        result["error"] = str(error)
        return result, reports
    work = fresh_directory(args.work_dir / args.name)
    work_corpus = fresh_directory(work / "corpus")
    command = libfuzzer_command(args, work_corpus, reports)
    result["command"] = command
    log_path = reports / LOG
    start = time.monotonic()
    try:
        with log_path.open("wb") as log:
            completed = subprocess.run(command, stdout=log,
                                       stderr=subprocess.STDOUT, cwd=work,
                                       check=False)
    except OSError as error:
        result["error"] = f"could not run {args.executable}: {error}"
        return result, reports
    result["seconds"] = round(time.monotonic() - start, 3)
    result["exit_code"] = completed.returncode
    output = log_path.read_text(encoding="utf-8", errors="replace")
    result["stats"] = {
        key: int(value) for key, value in STAT.findall(output)}
    artifacts = sorted(path for path in reports.iterdir()
                       if path.name not in (LOG, RESULT))
    result["artifacts"] = [str(path) for path in artifacts]
    result["finding"] = classify(output, artifacts)
    executed = result["stats"].get("number_of_executed_units", 0)
    if (completed.returncode == 0 and result["finding"] is None
            and executed < args.runs):
        result["error"] = (f"executed {executed} of the {args.runs} "
                           "budgeted runs")
    elif completed.returncode != 0 and result["finding"] is None:
        result["error"] = (f"libFuzzer exited with {completed.returncode} "
                           "without a recognized finding")
    return result, reports


def verdict(result, expect):
    if result["error"]:
        return False, result["error"]
    finding = result["finding"]
    exit_code = result["exit_code"]
    if expect is None:
        if finding or exit_code:
            return False, f"found a {finding} (exit {exit_code})"
        return True, "passed"
    reproducers = [path for path in result["artifacts"]
                   if pathlib.Path(path).name.startswith(f"{expect}-")]
    if finding != expect or not exit_code:
        return False, (f"expected a {expect} but found "
                       f"{finding or 'none'} (exit {exit_code})")
    if not reproducers:
        return False, f"found a {expect} but saved no {expect}- reproducer"
    return True, f"failed as expected with a {expect}"


def describe(result):
    stats = result["stats"]
    return (f"{stats.get('number_of_executed_units', 0)} runs in "
            f"{result['seconds']:.1f} s, "
            f"{stats.get('average_exec_per_sec', 0)} exec/s, "
            f"{stats.get('new_units_added', 0)} new units, "
            f"slowest unit {stats.get('slowest_unit_time_sec', 0)} s, "
            f"peak RSS {stats.get('peak_rss_mb', 0)} MB")


def main(argv=None):
    args = parse_arguments(argv)
    result, reports = run(args)
    passed, reason = verdict(result, args.expect)
    result["passed"] = passed
    result["verdict"] = reason
    (reports / RESULT).write_text(json.dumps(result, indent=2) + "\n",
                                  encoding="utf-8")
    label = "Fuzz canary" if args.expect else "Fuzz target"
    log_path = reports / LOG
    if not passed and log_path.is_file():
        sys.stderr.write(log_path.read_text(encoding="utf-8",
                                            errors="replace"))
    stream = sys.stdout if passed else sys.stderr
    print(f"{label} {args.name} {reason}: {describe(result)}.", file=stream)
    for artifact in result["artifacts"]:
        print(f"Reproducer: {artifact}", file=stream)
        print(f"Reproduce with: {args.executable} {artifact}", file=stream)
    print(f"Report: {reports}", file=stream)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
