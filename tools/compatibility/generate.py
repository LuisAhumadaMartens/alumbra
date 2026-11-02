import argparse
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = ROOT / "tools/compatibility/matrix.json"
DOC = ROOT / "docs/compatibility.md"
RELEASE = ROOT / "tools/compatibility/release-testing.json"
TIER_RULES = {
    "1": "Verified in CI on every change, or on a named device on a fixed cadence where CI cannot run it.",
    "2": "Verified on a reference device before each release.",
    "3": "Best effort. It builds or loads, but no promise, and a defect there does not block a release.",
}
SUPPORTED = re.compile(r"[●◐]")


class MatrixError(ValueError):
    pass


def load(path=DATA):
    return json.loads(path.read_text(encoding="utf-8"))


def validate(data):
    if not isinstance(data, dict):
        raise MatrixError("matrix root must be an object")
    if set(data) != {"schema_version", "tiers", "evidence", "platforms", "tables"}:
        raise MatrixError("matrix root has missing or unknown fields")
    if data.get("schema_version") != 1:
        raise MatrixError("schema_version must be 1")
    if not isinstance(data.get("tiers"), dict) or data.get("tiers") != TIER_RULES:
        raise MatrixError("tier definitions must match ADR-0039")
    evidence = data.get("evidence", {})
    if not isinstance(evidence, dict) or not evidence:
        raise MatrixError("evidence catalogue must not be empty")
    for key, item in evidence.items():
        if not isinstance(item, dict) or not item.get("label"):
            raise MatrixError(f"evidence {key} needs a label")
        if not isinstance(item.get("url"), str) or not item["url"].startswith("https://"):
            raise MatrixError(f"evidence {key} needs an https URL")
    platforms = data.get("platforms")
    if not isinstance(platforms, list):
        raise MatrixError("platforms must be an array")
    for platform in platforms:
        if not isinstance(platform, dict):
            raise MatrixError("platform entries must be objects")
        if not isinstance(platform.get("name"), str) or not isinstance(platform.get("floor"), str):
            raise MatrixError("platform name and floor must be strings")
        tier = str(platform.get("tier"))
        if tier not in TIER_RULES and tier != "n/a":
            raise MatrixError(f"{platform.get('name')}: invalid tier {tier}")
        if tier == "1" and "fixed cadence" not in data["tiers"][tier]:
            raise MatrixError(f"{platform.get('name')}: tier 1 cadence rule missing")
        check_evidence(platform.get("evidence"), evidence, platform.get("name", "platform"))
        for label, extra_tier in platform.get("additional_tiers", {}).items():
            if str(extra_tier) not in TIER_RULES:
                raise MatrixError(f"{platform['name']}: invalid tier for {label}")
    tables = data.get("tables")
    if not isinstance(tables, list):
        raise MatrixError("tables must be an array")
    for table in tables:
        if not isinstance(table, dict):
            raise MatrixError("table entries must be objects")
        columns = table.get("columns", [])
        if len(columns) < 2:
            raise MatrixError(f"{table.get('title')}: needs at least two columns")
        if not isinstance(table.get("title"), str) or not all(isinstance(column, str) for column in columns):
            raise MatrixError("table title and columns must be strings")
        rows = table.get("rows", [])
        if not isinstance(rows, list):
            raise MatrixError(f"{table.get('title')}: rows must be an array")
        for index, row in enumerate(rows):
            if not isinstance(row, list) or not all(isinstance(cell, str) for cell in row):
                raise MatrixError(f"{table.get('title')} row {index + 1}: cells must be strings")
            if len(row) != len(columns):
                raise MatrixError(f"{table['title']} row {index + 1}: cell count differs from columns")
            check_evidence([table.get("evidence")], evidence, f"{table['title']} row {index + 1}")
            for cell in row[1:]:
                if SUPPORTED.search(cell) and not table.get("evidence"):
                    raise MatrixError(f"{table['title']} row {index + 1}: supported cell has no evidence")
    if not platforms or not tables:
        raise MatrixError("platforms and tables must not be empty")


def check_evidence(keys, catalogue, where):
    if not keys or keys == [None]:
        raise MatrixError(f"{where}: evidence is required")
    for key in keys:
        if key not in catalogue:
            raise MatrixError(f"{where}: unknown evidence key {key}")


def markdown_table(headers, rows):
    result = ["| " + " | ".join(headers) + " |", "|" + "|".join("---" for _ in headers) + "|"]
    result.extend("| " + " | ".join(row) + " |" for row in rows)
    return "\n".join(result)


def evidence_links(keys, evidence):
    return ", ".join(f"[{evidence[key]['label']}]({evidence[key]['url']})" for key in keys)


def render_document(data):
    validate(data)
    evidence = data["evidence"]
    lines = [
        "# Compatibility matrix", "",
        "This matrix records Alumbra's target platforms and planned media compatibility. A support claim is only verified when the linked evidence meets its tier's cadence.", "",
        "## Support floors", "",
        "Tier 1 is verified in CI on every change, or on a named device on a fixed cadence where CI cannot run it. Tier 2 is verified on a reference device before each release. Tier 3 is best effort; defects do not block a release. Floors move only in a minor release.", "",
    ]
    floor_rows = []
    for platform in data["platforms"]:
        tier = str(platform["tier"])
        tier_text = tier
        if platform.get("additional_tiers"):
            extras = "; ".join(f"{name}: {value}" for name, value in platform["additional_tiers"].items())
            tier_text += "; " + extras
        floor_rows.append([platform["name"], platform["floor"], tier_text,
                           evidence_links(platform["evidence"], evidence)])
    lines.extend([markdown_table(["Platform", "Floor", "Tier", "Evidence"], floor_rows), "",
        "Web builds need WASM SIMD. Threads are optional ([ADR-0037](adr/0037-wasm-threading-and-cross-origin-isolation.md)). Toolchain floors are in [ADR-0013](adr/0013-cpp-language-standard.md).", ""])
    for table in data["tables"]:
        lines.extend([f"## {table['title']}", ""])
        out_rows = []
        for row in table["rows"]:
            cells = [row[0]]
            for cell in row[1:]:
                if SUPPORTED.search(cell):
                    cells.append(f"[{cell}]({evidence[table['evidence']]['url']})")
                else:
                    cells.append(cell)
            out_rows.append(cells)
        lines.extend([markdown_table(table["columns"], out_rows), ""])
        lines.extend(table.get("notes", []))
        if table.get("notes"):
            lines.append("")
    lines.extend(["## Evidence tiers", ""])
    for tier, description in TIER_RULES.items():
        lines.append(f"- **Tier {tier}:** {description}")
    lines.append("")
    return "\n".join(lines)


def render_release(data):
    validate(data)
    result = {"schema_version": 1, "platforms": [], "release_tests": []}
    for platform in data["platforms"]:
        if str(platform["tier"]) in ("1", "2"):
            result["platforms"].append({"name": platform["name"], "floor": platform["floor"],
                                        "tier": platform["tier"],
                                        "evidence": [data["evidence"][key] for key in platform["evidence"]]})
    for table in data["tables"]:
        for row in table["rows"]:
            for column, cell in zip(table["columns"][1:], row[1:]):
                if SUPPORTED.search(cell):
                    result["release_tests"].append({"area": table["title"], "capability": row[0],
                                                    "platform": column, "target": cell,
                                                    "evidence": data["evidence"][table["evidence"]]})
    return json.dumps(result, ensure_ascii=False, indent=2) + "\n"


def render():
    data = load()
    return {DOC: render_document(data), RELEASE: render_release(data)}


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    try:
        data = load()
        outputs = {DOC: render_document(data), RELEASE: render_release(data)}
        for path, content in outputs.items():
            if args.check:
                if not path.is_file() or path.read_text(encoding="utf-8") != content:
                    raise MatrixError(f"stale generated file: {path.relative_to(ROOT)}")
            else:
                path.write_text(content, encoding="utf-8")
    except (OSError, json.JSONDecodeError, MatrixError) as error:
        print(f"compatibility matrix: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
