import argparse
import json
import pathlib
import xml.etree.ElementTree as ElementTree

MARKS = {"supported": "✅ supported", "unsupported": "❌ unsupported",
         "run-failed": "⚠️ run-failed"}


def junit_section(label, path):
    if not path.exists():
        return [f"- **{label}:** no JUnit report (the step did not run)."]
    root = ElementTree.parse(path).getroot()
    passed, failed, skipped = 0, [], 0
    for case in root.iter("testcase"):
        status = case.get("status", "run")
        name = case.get("name", "?")
        if case.find("failure") is not None or case.find("error") is not None \
                or status == "fail":
            failed.append(name)
        elif case.find("skipped") is not None or status in ("notrun",
                                                            "disabled"):
            skipped += 1
        else:
            passed += 1
    lines = [f"- **{label}:** {passed} passed, {len(failed)} failed, "
             f"{skipped} skipped."]
    lines += [f"  - ❌ `{name}`" for name in failed]
    return lines


def toolchain_line(data):
    compiler = f"{data['compiler_id']} {data['compiler_version']}"
    if data.get("apple_toolchain"):
        compiler += (f" ({data['apple_toolchain']} "
                     f"{data['apple_toolchain_version']})")
    return (f"{compiler}, {data['standard_library']} "
            f"{data['standard_library_version']}, `__cplusplus` "
            f"{data['cplusplus']}, {data['target_system']} "
            f"{data['target_processor']}, mode `{data['mode']}`")


def probe_section(path):
    if not path.exists():
        return ["No C++ feature probe results were produced."]
    data = json.loads(path.read_text(encoding="utf-8"))
    lines = [f"**Toolchain:** {toolchain_line(data)}", "",
             "C++ feature probes for this toolchain only. A feature becomes "
             "allowed only through `docs/cpp-features.md`, with evidence from "
             "every pinned toolchain.", "",
             "| Probe | Policy | Result on this runner |",
             "|-------|--------|------------------------|"]
    for feature in data["features"]:
        lines.append(f"| `{feature['probe']}` | {feature['status']} | "
                     f"{MARKS.get(feature['result'], feature['result'])} |")
    return lines


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--title", required=True)
    parser.add_argument("--probe", type=pathlib.Path, required=True)
    parser.add_argument("--junit", action="append", default=[],
                        metavar="LABEL=PATH")
    args = parser.parse_args()
    lines = [f"## {args.title}", ""]
    for entry in args.junit:
        label, _, path = entry.partition("=")
        lines += junit_section(label, pathlib.Path(path))
    lines += [""] + probe_section(args.probe) + [""]
    print("\n".join(lines))


if __name__ == "__main__":
    main()
