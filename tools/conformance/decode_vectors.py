#!/usr/bin/env python3
import argparse
import hashlib
import pathlib
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

import manifest


def digest(path, algorithm):
    value = hashlib.new(algorithm)
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            value.update(block)
    return value.hexdigest()


def run(identifier, executable):
    entries = manifest.load()["entries"]
    selected = manifest.select(entries, ids=[identifier])
    entry = selected[0]
    vector = entry["decoder_vector"]
    with tempfile.TemporaryDirectory(prefix="alumbra-itu-") as temporary:
        archive = pathlib.Path(temporary) / "vector.zip"
        request = urllib.request.Request(entry["urls"][0], headers={"User-Agent": "alumbra-conformance/1"})
        with urllib.request.urlopen(request, timeout=60) as response, open(archive, "wb") as output:
            while True:
                block = response.read(1 << 20)
                if not block:
                    break
                output.write(block)
        if archive.stat().st_size != vector["archive_size"]:
            raise ValueError("archive size does not match the manifest")
        if digest(archive, "sha256") != vector["archive_sha256"]:
            raise ValueError("archive SHA-256 does not match the manifest")
        with zipfile.ZipFile(archive) as source:
            names = {pathlib.PurePosixPath(name): name for name in source.namelist()}
            stream_name = pathlib.PurePosixPath(vector["stream_member"])
            reference_name = pathlib.PurePosixPath(vector["reference_member"])
            if stream_name not in names or reference_name not in names:
                raise ValueError("archive is missing a declared vector member")
            stream_path = pathlib.Path(temporary) / "stream.bin"
            reference_path = pathlib.Path(temporary) / "reference.bin"
            stream_path.write_bytes(source.read(names[stream_name]))
            reference_path.write_bytes(source.read(names[reference_name]))
        expected = vector["reference_md5"]
        if vector["reference_is_digest"]:
            text = reference_path.read_text(encoding="ascii").strip().split()[0]
            if text.lower() != expected:
                raise ValueError("published MD5 file does not match the manifest")
        elif digest(reference_path, "md5") != expected:
            raise ValueError("reference YUV MD5 does not match the manifest")
        arguments = [
            executable,
            str(stream_path),
            vector["codec"],
            expected,
            str(vector["width"]),
            str(vector["height"]),
            str(vector["frames"]),
            str(vector["bit_depth"]),
        ]
        result = subprocess.run(arguments, check=False)
        return result.returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--id", required=True)
    parser.add_argument("--test-executable", required=True)
    arguments = parser.parse_args()
    try:
        return run(arguments.id, arguments.test_executable)
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"{arguments.id}: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
