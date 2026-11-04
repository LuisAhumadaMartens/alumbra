import pathlib
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]


def run(command):
    return subprocess.run(command, check=False, cwd=ROOT).returncode


def main():
    if shutil.which("ffmpeg") is None:
        print("SKIP: ffmpeg is unavailable for A/V capture generation")
        return 77
    runner, analyser = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="alumbra-av-sync-") as directory:
        root = pathlib.Path(directory)
        clip = root / "capture.mp4"
        capture = root / "capture.json"
        report = root / "report.json"
        result = run([
            sys.executable,
            str(ROOT / "tools/av_sync/generate_clip.py"),
            str(clip),
            "--container", "mp4",
            "--fps", "25",
            "--sample-rate", "48000",
            "--duration", "8",
        ])
        if result != 0:
            return result
        result = run([runner, str(clip), str(capture)])
        if result != 0:
            return result
        return run([
            sys.executable,
            analyser,
            "analyse", str(capture), "--json", str(report),
            "--plot", str(root / "report.svg"), "--enforce",
        ])


if __name__ == "__main__":
    raise SystemExit(main())
