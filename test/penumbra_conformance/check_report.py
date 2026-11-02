import argparse
import json
import pathlib
import subprocess
import sys

RESULTS = {"passed", "skipped"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    output = pathlib.Path(args.output)
    output.unlink(missing_ok=True)
    run = subprocess.run([args.executable, f"--conformance_report={output}"])
    if run.returncode:
        return run.returncode
    report = json.loads(output.read_text(encoding="utf-8"))
    if report["schema"] != 1 or report["interface_version"] < 1:
        print("unexpected report header", file=sys.stderr)
        return 1
    ports = {port["name"]: port for port in report["ports"]}
    if set(ports) != {args.port}:
        print(f"expected only port {args.port}, found {sorted(ports)}",
              file=sys.stderr)
        return 1
    port = ports[args.port]
    if port["interface_version"] != report["interface_version"]:
        print("port interface version differs", file=sys.stderr)
        return 1
    tests = port["tests"]
    if not tests:
        print("report has no tests", file=sys.stderr)
        return 1
    bad = [t for t in tests if t["result"] not in RESULTS]
    if bad:
        print(f"unexpected results: {bad}", file=sys.stderr)
        return 1
    names = {(t["suite"], t["test"]) for t in tests}
    if len(names) != len(tests):
        print("duplicate test entries", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
