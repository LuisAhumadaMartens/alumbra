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
SITE_FILES = frozenset({".typos-adr.toml", ".typos.toml", ".vale.ini"})
SITE_PREFIXES = (
    "docs/", "tools/site/", "tools/vale/", "settings/", ".github/workflows/",
    "tools/ci/")
PUBLIC_HEADER_LAYERS = ("umbra/", "penumbra/", "antumbra/")
ZERO_SHA = "0" * 40


def is_docs_only(path):
    if path in CODE_DOCS:
        return False
    if path in DOCS_ONLY_FILES or path.startswith(DOCS_ONLY_PREFIXES):
        return True
    if "/" not in path and path.endswith(".md"):
        return True
    return path.endswith("/README.md")


def is_site_input(path):
    if path in SITE_FILES or path.startswith(SITE_PREFIXES):
        return True
    if path.endswith(".md"):
        return True
    return path.startswith(PUBLIC_HEADER_LAYERS) and "/include/" in path


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
        cxx = site = True
    elif not files:
        reason = "full run: the diff is empty"
        cxx = site = True
    else:
        code = [path for path in files if not is_docs_only(path)]
        cxx = bool(code)
        site = any(is_site_input(path) for path in files)
        reason = (f"{len(code)} of {len(files)} changed files need the C++ matrix"
                  if cxx else f"all {len(files)} changed files are documentation")
        reason += ("; the documentation site and prose checks run" if site
                   else "; no change reaches the documentation site")
    print(f"cxx={'true' if cxx else 'false'}")
    print(f"site={'true' if site else 'false'}")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as stream:
            stream.write(f"### Change classification\n\n{reason}.\n")
            for path in files or []:
                kind = "docs" if is_docs_only(path) else "code"
                if is_site_input(path):
                    kind += ", site"
                stream.write(f"- `{path}` ({kind})\n")
    print(reason, file=sys.stderr)


if __name__ == "__main__":
    main()
