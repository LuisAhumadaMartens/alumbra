import pathlib
import re
import sys

import yaml

PINNED = re.compile(r"^[\w.-]+/[\w./-]+@[0-9a-f]{40}$")
DOCKER = re.compile(r"^docker://[^@]+@sha256:[0-9a-f]{64}$")
LOCAL = re.compile(r"^\./[\w./-]+$")


def uses_entries(node, visited):
    if node is None or id(node) in visited:
        return
    visited.add(id(node))
    if isinstance(node, yaml.MappingNode):
        for key, value in node.value:
            if isinstance(key, yaml.ScalarNode) and key.value == "uses":
                yield key.start_mark.line + 1, value
            yield from uses_entries(value, visited)
    elif isinstance(node, yaml.SequenceNode):
        for value in node.value:
            yield from uses_entries(value, visited)


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".github")
    files = sorted(root.rglob("*.yml")) + sorted(root.rglob("*.yaml"))
    failures = []
    count = 0
    for path in files:
        try:
            workflow = yaml.compose(path.read_text(), Loader=yaml.SafeLoader)
        except yaml.YAMLError as error:
            failures.append(f"{path}: invalid YAML: {error}")
            continue
        for number, value in uses_entries(workflow, set()):
            count += 1
            target = value.value if isinstance(value, yaml.ScalarNode) \
                and value.tag == "tag:yaml.org,2002:str" else None
            if target is not None and (LOCAL.fullmatch(target)
                                       or PINNED.fullmatch(target)
                                       or DOCKER.fullmatch(target)):
                continue
            failures.append(f"{path}:{number}: {target!r} is not pinned to a "
                            "full commit SHA, Docker digest or local action")
    for failure in failures:
        print(failure, file=sys.stderr)
    if not count:
        print(f"No 'uses:' entries found under {root}", file=sys.stderr)
        return 1
    print(f"{count} 'uses:' entries checked, {len(failures)} rejected")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
