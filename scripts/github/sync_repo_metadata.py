#!/usr/bin/env python3
"""Syncs repository labels and milestones from their sources of truth.

- Labels:     .github/labels.yml
- Milestones: docs/roadmap.md ("## M<n> · <name>" sections and their exit criteria)

Idempotent: creates missing items and updates changed ones. With --prune, deletes labels that are not
declared in .github/labels.yml. Requires the GitHub CLI (`gh`) authenticated with `repo` scope, and PyYAML.

Usage: scripts/github/sync_repo_metadata.py [--repo OWNER/NAME] [--prune] [--dry-run]
"""
import argparse
import json
import pathlib
import re
import subprocess
import sys
import urllib.parse

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
LABELS_FILE = ROOT / ".github" / "labels.yml"
ROADMAP_FILE = ROOT / "docs" / "roadmap.md"
MAX_DESCRIPTION = 1000


def gh_api(path, method="GET", fields=None, dry_run=False):
    if dry_run and method != "GET":
        print(f"  [dry-run] {method} {path} {json.dumps(fields or {})}")
        return {}
    cmd = ["gh", "api", "--method", method, path]
    if method == "GET":
        cmd += ["--paginate", "--slurp"]
    for key, value in (fields or {}).items():
        cmd += ["-f", f"{key}={value}"]
    out = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    if not out.strip():
        return {}
    data = json.loads(out)
    if method == "GET":
        return [item for page in data for item in page]
    return data


def default_repo():
    out = subprocess.run(["gh", "repo", "view", "--json", "nameWithOwner", "-q", ".nameWithOwner"],
                         check=True, capture_output=True, text=True, cwd=ROOT).stdout
    return out.strip()


def slugify(heading):
    """Approximates GitHub's heading anchor algorithm."""
    slug = heading.strip().lower()
    slug = re.sub(r"[^\w\- ]", "", slug)
    return slug.replace(" ", "-")


def parse_roadmap(repo):
    milestones, current = [], None
    for line in ROADMAP_FILE.read_text().splitlines():
        heading = re.match(r"^## (M\d+) · (.+)$", line)
        if heading:
            current = {"key": heading.group(1), "title": f"{heading.group(1)} · {heading.group(2)}",
                       "anchor": slugify(line[3:]), "lines": []}
            milestones.append(current)
        elif line.startswith("## "):
            current = None
        elif current is not None and line.strip():
            text = line.strip()
            if line.startswith("  ") and current["lines"]:
                current["lines"][-1] += " " + text  # Wrapped bullet continuation.
            else:
                current["lines"].append(text)
    for m in milestones:
        text = "\n".join(m["lines"])
        text = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)  # Relative links do not resolve here.
        link = f"\n\nExit criteria: https://github.com/{repo}/blob/main/docs/roadmap.md#{m['anchor']}"
        if len(text) + len(link) > MAX_DESCRIPTION:
            text = text[: MAX_DESCRIPTION - len(link) - 2].rsplit("\n", 1)[0] + "\n…"
        m["description"] = text + link
    return milestones


def sync_labels(repo, prune, dry_run):
    wanted = {label["name"]: label for label in yaml.safe_load(LABELS_FILE.read_text())}
    existing = {label["name"]: label for label in gh_api(f"repos/{repo}/labels?per_page=100")}
    for name, label in wanted.items():
        fields = {"color": label["color"], "description": label["description"]}
        current = existing.get(name)
        if current is None:
            print(f"label + {name}")
            gh_api(f"repos/{repo}/labels", "POST", {"name": name, **fields}, dry_run)
        elif current["color"].lower() != fields["color"] or (current["description"] or "") != fields["description"]:
            print(f"label ~ {name}")
            gh_api(f"repos/{repo}/labels/{urllib.parse.quote(name)}", "PATCH", fields, dry_run)
    if prune:
        for name in sorted(set(existing) - set(wanted)):
            print(f"label - {name}")
            gh_api(f"repos/{repo}/labels/{urllib.parse.quote(name)}", "DELETE", dry_run=dry_run)


def sync_milestones(repo, dry_run):
    existing = gh_api(f"repos/{repo}/milestones?state=all&per_page=100")
    by_key = {m["title"].split(" · ")[0]: m for m in existing}
    for m in parse_roadmap(repo):
        current = by_key.get(m["key"])
        if current is None:
            print(f"milestone + {m['title']}")
            gh_api(f"repos/{repo}/milestones", "POST",
                   {"title": m["title"], "description": m["description"]}, dry_run)
        elif current["title"] != m["title"] or (current["description"] or "") != m["description"]:
            print(f"milestone ~ {m['title']}")
            gh_api(f"repos/{repo}/milestones/{current['number']}", "PATCH",
                   {"title": m["title"], "description": m["description"]}, dry_run)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", help="OWNER/NAME (defaults to the current repository)")
    parser.add_argument("--prune", action="store_true", help="delete labels not declared in labels.yml")
    parser.add_argument("--dry-run", action="store_true", help="print changes without applying them")
    args = parser.parse_args()
    repo = args.repo or default_repo()
    sync_labels(repo, args.prune, args.dry_run)
    sync_milestones(repo, args.dry_run)
    return 0


if __name__ == "__main__":
    sys.exit(main())
