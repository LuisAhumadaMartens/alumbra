import argparse
import filecmp
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SHADER_DIR = ROOT / "penumbra/desktop/video/shaders"
GENERATED_DIR = SHADER_DIR / "generated"
STAGES = {".vert": "vertex", ".frag": "fragment", ".comp": "compute"}
TARGETS = {"spv": "SPIRV", "msl": "MSL"}
ENTRY_POINT = "main"


def stage_of(source):
    stage = STAGES.get(pathlib.Path(source.stem).suffix)
    if stage is None:
        raise ValueError(f"{source.name}: name must be <name>.<vert|frag|comp>.hlsl")
    return stage


def sources(shader_dir):
    return sorted(shader_dir.glob("*.hlsl"))


def output_name(source, extension):
    return f"{source.stem}.{extension}"


def command(shadercross, source, destination, target):
    return [str(shadercross), str(source), "-s", "HLSL", "-d", TARGETS[target],
            "-t", stage_of(source), "-e", ENTRY_POINT, "-o", str(destination)]


def generate(shadercross, shader_dir, output_dir, library_path):
    output_dir.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    if library_path:
        env["LD_LIBRARY_PATH"] = os.pathsep.join(
            [str(library_path)] + ([env["LD_LIBRARY_PATH"]] if env.get("LD_LIBRARY_PATH") else []))
    produced = []
    for source in sources(shader_dir):
        for target in TARGETS:
            destination = output_dir / output_name(source, target)
            subprocess.run(command(shadercross, source, destination, target),
                           check=True, env=env)
            produced.append(destination.name)
    return produced


def differences(produced, generated_dir, output_dir):
    problems = []
    for name in produced:
        committed = generated_dir / name
        if not committed.exists():
            problems.append(f"{name}: missing from {generated_dir}")
        elif not filecmp.cmp(committed, output_dir / name, shallow=False):
            problems.append(f"{name}: differs from the committed output")
    expected = set(produced)
    for stale in sorted(generated_dir.glob("*")):
        if stale.name not in expected:
            problems.append(f"{stale.name}: has no HLSL source")
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Compile the desktop HLSL shaders to SPIR-V and MSL with "
                    "SDL_shadercross.")
    parser.add_argument("--shadercross", type=pathlib.Path, required=True)
    parser.add_argument("--library-path", type=pathlib.Path)
    parser.add_argument("--check", action="store_true",
                        help="regenerate and compare instead of overwriting")
    parser.add_argument("--output-dir", type=pathlib.Path,
                        help="write regenerated files here when checking")
    args = parser.parse_args(argv)
    if not args.check:
        generate(args.shadercross, SHADER_DIR, GENERATED_DIR, args.library_path)
        return 0
    with tempfile.TemporaryDirectory() as scratch:
        output_dir = args.output_dir or pathlib.Path(scratch)
        produced = generate(args.shadercross, SHADER_DIR, output_dir,
                            args.library_path)
        problems = differences(produced, GENERATED_DIR, output_dir)
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        print("Regenerate with: python3 tools/shaders/generate.py "
              "--shadercross <path to shadercross>", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
