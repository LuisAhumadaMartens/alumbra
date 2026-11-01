import os
import json
import pathlib
import re
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
LLVM_MAJOR = (ROOT / "tools/ci/llvm-major").read_text().strip()
EXTENSIONS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
              ".m", ".mm"}
EXCLUDED = {"build", "out", "generated", "third_party", "third-party",
            "vendor", "vcpkg", "vcpkg_installed", ".git"}


def source_files():
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=ROOT, capture_output=True, check=True)
    return sorted({os.fsdecode(name) for name in result.stdout.split(b"\0")
                   if name and is_source(os.fsdecode(name))})


def is_source(name):
    path = pathlib.PurePath(name)
    parts = path.parts
    excluded = EXCLUDED - {"build"} if parts[:2] == ("test", "build") else EXCLUDED
    return path.suffix in EXTENSIONS and not excluded.intersection(parts)


def translation_units(build):
    entries = json.loads((build / "compile_commands.json").read_text())
    selected = []
    for entry in entries:
        file = pathlib.Path(entry["file"])
        if not file.is_absolute():
            file = pathlib.Path(entry["directory"]) / file
        file = file.resolve()
        if file.is_relative_to(ROOT) and is_source(str(file.relative_to(ROOT))):
            selected.append((file, entry))
    if not selected:
        raise RuntimeError("No Alumbra translation units in compile_commands.json")
    return selected


def llvm_tool(name):
    directory = os.environ.get("LLVM_BIN_DIR")
    candidates = ([str(pathlib.Path(directory) / name)] if directory else
                  [f"{name}-{LLVM_MAJOR}", name])
    for candidate in candidates:
        executable = shutil.which(candidate)
        if executable:
            version = subprocess.check_output(
                [executable, "--version"], text=True)
            if re.search(rf"\bversion {LLVM_MAJOR}\.", version):
                print(version.strip(), flush=True)
                return executable
    raise RuntimeError(
        f"{name} from LLVM {LLVM_MAJOR} is required; set LLVM_BIN_DIR "
        "to its bin directory")
