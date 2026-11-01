import argparse
import json
import pathlib


def rows(report_dir):
    results = []
    for path in sorted(report_dir.glob("*/result.json")):
        try:
            results.append(json.loads(path.read_text(encoding="utf-8")))
        except (OSError, ValueError) as error:
            results.append({"name": path.parent.name, "passed": False,
                            "verdict": f"unreadable report: {error}"})
    return results


def render(title, report_dir):
    lines = [f"## {title}", ""]
    results = rows(report_dir)
    if not results:
        return "\n".join(lines + ["No fuzz reports were produced.", ""])
    budget = next((result["budget"] for result in results
                   if "budget" in result), None)
    if budget:
        lines += [f"Budget per target: `-runs={budget['runs']}` "
                  f"`-seed={budget['seed']}` "
                  f"`-rss_limit_mb={budget['rss_limit_mb']}` "
                  f"`-timeout={budget['timeout']}`.", ""]
    lines += ["| Target | Result | Runs | Seconds | exec/s | New units | "
              "Slowest unit (s) | Peak RSS (MB) | Reproducers |",
              "|--------|--------|------|---------|--------|-----------|"
              "------------------|---------------|-------------|"]
    for result in results:
        stats = result.get("stats", {})
        mark = "✅" if result.get("passed") else "❌"
        reproducers = ", ".join(
            f"`{pathlib.Path(path).name}`"
            for path in result.get("artifacts", [])) or "none"
        lines.append(
            f"| `{result['name']}` | {mark} {result.get('verdict', '')} | "
            f"{stats.get('number_of_executed_units', 0)} | "
            f"{result.get('seconds', 0)} | "
            f"{stats.get('average_exec_per_sec', 0)} | "
            f"{stats.get('new_units_added', 0)} | "
            f"{stats.get('slowest_unit_time_sec', 0)} | "
            f"{stats.get('peak_rss_mb', 0)} | {reproducers} |")
    return "\n".join(lines + [""])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--title", required=True)
    parser.add_argument("--report-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()
    print(render(args.title, args.report_dir))


if __name__ == "__main__":
    main()
