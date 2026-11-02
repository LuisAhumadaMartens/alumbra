import argparse
import hashlib
import platform
import shutil
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path

VERSION = "1.18.0"
RELEASE = f"https://github.com/doxygen/doxygen/releases/download/Release_{VERSION.replace('.', '_')}"
ARCHIVES = {
    ("Linux", "x86_64"): (
        f"{RELEASE}/doxygen-{VERSION}.linux.bin.tar.gz",
        "14fa81bdc34171edb5f1f02b1d60e74802f0439b77fa44e592565d517d72df90",
        f"doxygen-{VERSION}/bin/doxygen"),
    ("Darwin", "arm64"): (
        f"{RELEASE}/doxygen-{VERSION}-mac-arm.zip",
        "31d1c74467a9f6f456b4e6a4eff0c03e93d1ffa92da5fdf2d53a2f14ebaabbd7",
        f"doxygen-{VERSION}/doxygen"),
}


def archive_for(system, machine):
    try:
        return ARCHIVES[(system, machine)]
    except KeyError:
        raise RuntimeError(
            f"No pinned Doxygen {VERSION} download for {system} {machine}; "
            f"install Doxygen {VERSION} and pass its path with --doxygen") from None


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def extract(archive, member, destination):
    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive) as bundle, bundle.open(member) as source:
            with destination.open("wb") as target:
                shutil.copyfileobj(source, target)
        return
    with tarfile.open(archive) as bundle:
        source = bundle.extractfile(member)
        if source is None:
            raise RuntimeError(f"{member} is not a regular file in {archive.name}")
        with source, destination.open("wb") as target:
            shutil.copyfileobj(source, target)


def install(directory, system=None, machine=None):
    url, expected, member = archive_for(system or platform.system(),
                                        machine or platform.machine())
    directory = Path(directory).resolve()
    binary = directory / VERSION / "doxygen"
    if binary.is_file():
        return binary
    binary.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=directory) as scratch:
        archive = Path(scratch) / url.rsplit("/", 1)[1]
        with urllib.request.urlopen(url, timeout=120) as response:
            with archive.open("wb") as stream:
                shutil.copyfileobj(response, stream)
        actual = sha256(archive)
        if actual != expected:
            raise RuntimeError(
                f"{archive.name} has SHA-256 {actual}, expected {expected}")
        staged = Path(scratch) / "doxygen"
        extract(archive, member, staged)
        staged.chmod(0o755)
        staged.replace(binary)
    return binary


def main():
    parser = argparse.ArgumentParser(
        description=f"Download the pinned Doxygen {VERSION} and print its path")
    parser.add_argument("--install-dir", required=True, type=Path)
    args = parser.parse_args()
    try:
        print(install(args.install_dir))
    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
