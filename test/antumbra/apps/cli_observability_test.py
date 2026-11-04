import subprocess
import sys


def run(executable, fixture, *options):
    return subprocess.run(
        [executable, "--headless", *options, fixture],
        capture_output=True,
        text=True,
        check=False,
        timeout=30,
    )


def main():
    executable, fixture = sys.argv[1:]
    info = run(executable, fixture, "--log-level", "info")
    if info.returncode != 0 or "load requested" not in info.stderr:
        raise SystemExit("info log level did not emit player logs")
    warning = run(executable, fixture, "--log-level", "warning")
    if warning.returncode != 0 or "load requested" in warning.stderr:
        raise SystemExit("warning log level did not filter info logs")
    traced = run(executable, fixture, "--trace", "player")
    if traced.returncode != 0 or "player.load" not in traced.stderr:
        raise SystemExit("player trace category did not emit trace output")
    configured = run(
        executable,
        fixture,
        "--set",
        "buffer.start_threshold_ms=0",
    )
    if configured.returncode != 0:
        raise SystemExit("--set assignment did not reach player creation")
    rejected = run(
        executable,
        fixture,
        "--set",
        "buffer.start_threshold_ms=60001",
    )
    if rejected.returncode != 2 or "outside the allowed range" not in rejected.stderr:
        raise SystemExit("--set did not report a schema range error")


if __name__ == "__main__":
    main()
