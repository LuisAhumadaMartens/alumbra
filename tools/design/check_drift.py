import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

from generate_tokens import render
from token_model import COMMAND, ROOT, load

SOURCES = ("design/tokens",)
GIT = ("git", "-c", "user.name=drift", "-c", "user.email=drift@invalid",
       "-c", "commit.gpgsign=false", "-c", "core.autocrlf=false")


def git(directory, *arguments, check=True):
    return subprocess.run(
        GIT + arguments, cwd=directory, check=check, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def copy_into(root, scratch, relative):
    source = root / relative
    target = scratch / relative
    if source.is_dir():
        shutil.copytree(source, target)
    elif source.is_file():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def check(root):
    outputs = sorted(render(load(root / "design/tokens")))
    with tempfile.TemporaryDirectory() as directory:
        scratch = pathlib.Path(directory)
        for relative in (*SOURCES, *outputs):
            copy_into(root, scratch, relative)
        git(scratch, "init", "-q")
        git(scratch, "add", "-A")
        git(scratch, "commit", "-q", "-m", "snapshot")
        generated = render(load(scratch / "design/tokens"))
        for relative, content in generated.items():
            path = scratch / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content.encode("utf-8"))
        git(scratch, "add", "--intent-to-add", "-A")
        result = git(scratch, "diff", "--exit-code", check=False)
    if result.returncode == 0:
        return ""
    return (
        "generated design-token outputs differ from what design/tokens generates:\n"
        f"{result.stdout}\nregenerate them with: {COMMAND}"
    )


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Regenerate the token outputs in a clean copy and fail on any diff")
    parser.add_argument("--root", type=pathlib.Path, default=ROOT)
    args = parser.parse_args(argv)
    try:
        message = check(args.root.resolve())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1
    if message:
        print(message, file=sys.stderr)
        return 1
    print("generated design-token outputs match design/tokens")
    return 0


if __name__ == "__main__":
    sys.exit(main())
