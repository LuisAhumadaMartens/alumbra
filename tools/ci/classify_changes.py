import os
import subprocess
import sys

DOCS_ONLY_FILES = frozenset({
    ".github/CODEOWNERS",
    ".github/labels.yml",
    ".github/pull_request_template.md",
    "LICENSE",
    "NOTICE",
})
DOCS_ONLY_PREFIXES = ("docs/", ".github/ISSUE_TEMPLATE/")
CODE_DOCS = frozenset({"docs/cpp-features.md"})
REFERENCE_FILES = frozenset({".github/workflows/ci.yml"})
REFERENCE_PREFIXES = ("docs/api/", "tools/cpp-reference/",
                      "test/consumer/examples/", "errors/", "tools/errors/")
PUBLIC_HEADER_LAYERS = ("umbra", "penumbra")
ZERO_SHA = "0" * 40


def is_docs_only(path):
    if path in CODE_DOCS:
        return False
    if path in DOCS_ONLY_FILES or path.startswith(DOCS_ONLY_PREFIXES):
        return True
    if "/" not in path and path.endswith(".md"):
        return True
    return path.endswith("/README.md")


def is_reference_input(path):
    if path in REFERENCE_FILES or path.startswith(REFERENCE_PREFIXES):
        return True
    parts = path.split("/")
    return parts[0] in PUBLIC_HEADER_LAYERS and "include" in parts[1:-1]


def changed_files():
    event = os.environ.get("EVENT", "")
    if event == "pull_request":
        revisions = [f"{os.environ['BASE']}...{os.environ['HEAD']}"]
    elif event == "push" and os.environ.get("BEFORE", ZERO_SHA) != ZERO_SHA:
        revisions = [os.environ["BEFORE"], os.environ["SHA"]]
    else:
        return None
    result = subprocess.run(
        ["git", "diff", "--name-only", "--no-renames", "-z", *revisions],
        capture_output=True, check=False)
    if result.returncode != 0:
        print(os.fsdecode(result.stderr), file=sys.stderr)
        return None
    return [os.fsdecode(path) for path in result.stdout.split(b"\0") if path]


def main():
    files = changed_files()
    if files is None:
        reason = "full run: no comparable base for this event"
        cxx = reference = True
    elif not files:
        reason = "full run: the diff is empty"
        cxx = reference = True
    else:
        code = [path for path in files if not is_docs_only(path)]
        cxx = bool(code)
        reference = any(is_reference_input(path) for path in files)
        reason = (f"{len(code)} of {len(files)} changed files need the C++ matrix"
                  if cxx else f"all {len(files)} changed files are documentation")
        reason += ("; the C++ API reference is affected" if reference
                   else "; the C++ API reference is not affected")
    print(f"cxx={'true' if cxx else 'false'}")
    print(f"cpp_reference={'true' if reference else 'false'}")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as stream:
            stream.write(f"### Change classification\n\n{reason}.\n")
            for path in files or []:
                kind = "docs" if is_docs_only(path) else "code"
                if is_reference_input(path):
                    kind += ", reference"
                stream.write(f"- `{path}` ({kind})\n")
    print(reason, file=sys.stderr)


if __name__ == "__main__":
    main()
