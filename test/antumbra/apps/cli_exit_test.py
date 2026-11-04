import subprocess
import sys


def main():
    result = subprocess.run(
        [sys.argv[1], "--headless", sys.argv[2]],
        capture_output=True,
        check=False,
        timeout=15,
    )
    expected = int(sys.argv[3])
    if result.returncode != expected:
        sys.stderr.write(result.stderr.decode(errors="replace"))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
