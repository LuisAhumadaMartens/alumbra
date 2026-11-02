import os
import subprocess
import sys

DOCS_ONLY_FILES = frozenset({
    ".github/CODEOWNERS",
    ".github/labels.yml",
    ".github/pull_request_template.md",
    ".typos-adr.toml",
    ".typos.toml",
    ".vale.ini",
    "LICENSE",
    "NOTICE",
})
DOCS_ONLY_PREFIXES = (
    "docs/", ".github/ISSUE_TEMPLATE/", "tools/site/", "tools/vale/")
CODE_DOCS = frozenset({"docs/cpp-features.md"})
TOKEN_PREFIXES = (
    "design/", "tools/design/", "antumbra/", ".github/workflows/", "tools/ci/")
TOKEN_DOCS = frozenset({"docs/reference/design-tokens.md"})
REFERENCE_FILES = frozenset({".github/workflows/ci.yml"})
REFERENCE_PREFIXES = ("docs/api/", "tools/cpp-reference/",
                      "test/consumer/examples/", "errors/", "tools/errors/")
PUBLIC_HEADER_LAYERS = ("umbra", "penumbra", "antumbra")
SITE_FILES = frozenset({".typos-adr.toml", ".typos.toml", ".vale.ini"})
SITE_PREFIXES = (
    "docs/", "tools/site/", "tools/vale/", "settings/", ".github/workflows/",
    "tools/ci/")
SITE_HEADER_PREFIXES = ("umbra/", "penumbra/", "antumbra/")
ZERO_SHA = "0" * 40


def is_docs_only(path):
    if path in CODE_DOCS:
        return False
    if path in DOCS_ONLY_FILES or path.startswith(DOCS_ONLY_PREFIXES):
        return True
    if "/" not in path and path.endswith(".md"):
        return True
    return path.endswith("/README.md")


def needs_tokens(path):
    if path in TOKEN_DOCS:
        return True
    return not is_docs_only(path) and path.startswith(TOKEN_PREFIXES)


def is_reference_input(path):
    if path in REFERENCE_FILES or path.startswith(REFERENCE_PREFIXES):
        return True
    parts = path.split("/")
    return parts[0] in PUBLIC_HEADER_LAYERS and "include" in parts[1:-1]


def is_site_input(path):
    if path in SITE_FILES or path.startswith(SITE_PREFIXES):
        return True
    if path.endswith(".md"):
        return True
    return path.startswith(SITE_HEADER_PREFIXES) and "/include/" in path


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


def tracked_markdown():
    result = subprocess.run(
        ["git", "ls-files", "-z", "--", "*.md"], capture_output=True,
        check=True)
    return [os.fsdecode(path) for path in result.stdout.split(b"\0") if path]


def changed_markdown():
    files = changed_files()
    if not files:
        return tracked_markdown()
    return [path for path in files
            if path.endswith(".md") and os.path.isfile(path)]


def main():
    if sys.argv[1:] == ["--changed-markdown"]:
        for path in changed_markdown():
            sys.stdout.buffer.write(os.fsencode(path) + b"\0")
        return
    files = changed_files()
    if files is None:
        reason = "full run: no comparable base for this event"
        cxx = tokens = reference = site = True
    elif not files:
        reason = "full run: the diff is empty"
        cxx = tokens = reference = site = True
    else:
        code = [path for path in files if not is_docs_only(path)]
        cxx = bool(code)
        tokens = any(needs_tokens(path) for path in files)
        reference = any(is_reference_input(path) for path in files)
        site = any(is_site_input(path) for path in files)
        reason = (f"{len(code)} of {len(files)} changed files need the C++ matrix"
                  if cxx else f"all {len(files)} changed files are documentation")
        reason += ("; design-token outputs are checked" if tokens
                   else "; no design-token input changed")
        reason += ("; the C++ API reference is affected" if reference
                   else "; the C++ API reference is not affected")
        reason += ("; the documentation site and prose checks run" if site
                   else "; no change reaches the documentation site")
    print(f"cxx={'true' if cxx else 'false'}")
    print(f"tokens={'true' if tokens else 'false'}")
    print(f"cpp_reference={'true' if reference else 'false'}")
    print(f"site={'true' if site else 'false'}")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as stream:
            stream.write(f"### Change classification\n\n{reason}.\n")
            for path in files or []:
                kind = "docs" if is_docs_only(path) else "code"
                if is_reference_input(path):
                    kind += ", reference"
                if is_site_input(path):
                    kind += ", site"
                stream.write(f"- `{path}` ({kind})\n")
    print(reason, file=sys.stderr)


if __name__ == "__main__":
    main()
