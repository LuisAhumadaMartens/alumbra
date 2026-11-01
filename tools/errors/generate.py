import argparse
import json
from pathlib import Path
import re
import sys
import tomllib

CATEGORIES = ("network", "manifest", "demux", "decode", "drm", "text", "ads",
              "player")
SEVERITIES = ("recoverable", "degraded", "fatal")
CANONICAL = ("Cancelled", "Unknown", "InvalidArgument", "DeadlineExceeded",
             "NotFound", "AlreadyExists", "PermissionDenied",
             "ResourceExhausted", "FailedPrecondition", "Aborted", "OutOfRange",
             "Unimplemented", "Internal", "Unavailable", "DataLoss",
             "Unauthenticated")
FIELDS = {"code", "name", "category", "severity", "retryable", "media_error",
          "canonical", "description"}
ROOT = Path(__file__).resolve().parents[2]


def load(path):
    with path.open("rb") as stream:
        source = tomllib.load(stream)
    if (set(source) != {"version", "error"}
            or type(source["version"]) is not int or source["version"] != 1):
        raise ValueError("catalogue requires version 1 and error records")
    records = source["error"]
    if not isinstance(records, list) or not records:
        raise ValueError("catalogue must not be empty")
    codes, names = set(), set()
    for record in records:
        if not isinstance(record, dict) or set(record) != FIELDS:
            raise ValueError("error record has missing or unknown fields")
        code, name = record["code"], record["name"]
        category = record["category"]
        if (type(code) is not int or category not in CATEGORIES
                or code // 1000 != CATEGORIES.index(category) + 1):
            raise ValueError("catalogue code is outside its category range")
        if (not isinstance(name, str)
                or not re.fullmatch(r"[A-Z][a-zA-Z0-9]*", name)
                or code in codes or name in names):
            raise ValueError("catalogue names and codes must be unique")
        if (record["severity"] not in SEVERITIES
                or type(record["retryable"]) is not bool
                or type(record["media_error"]) is not int
                or record["media_error"] not in (1, 2, 3, 4)
                or record["canonical"] not in CANONICAL):
            raise ValueError("invalid error metadata")
        text = record["description"]
        if (not isinstance(text, str) or not text
                or not re.fullmatch(r"[A-Za-z0-9 ,().'-]+", text)):
            raise ValueError("description must be plain diagnostic text")
        codes.add(code)
        names.add(name)
    if set(r["category"] for r in records) != set(CATEGORIES):
        raise ValueError("catalogue must cover every category")
    return sorted(records, key=lambda r: r["code"])


def render(records):
    header = [
        "#ifndef UMBRA_BASE_ERROR_CATALOGUE_H_",
        "#define UMBRA_BASE_ERROR_CATALOGUE_H_", "",
        "#include <array>", "#include <cstdint>", "#include <string_view>", "",
        "namespace umbra::base {", "", "enum class ErrorCategory {",
        *[f"  k{c.title()}," for c in CATEGORIES], "};", "",
        "enum class ErrorSeverity {",
        *[f"  k{s.title()}," for s in SEVERITIES], "};", "",
        "enum class ErrorCode : std::uint32_t {",
        *[f'  k{r["name"]} = {r["code"]},' for r in records], "};", "",
        "struct ErrorCatalogueEntry {",
        "  ErrorCode code;", "  std::string_view name;",
        "  ErrorCategory category;", "  ErrorSeverity severity;",
        "  bool retryable;", "  int media_error;",
        "  std::string_view canonical;", "  std::string_view description;",
        "};", "",
        f"inline constexpr std::array<ErrorCatalogueEntry, {len(records)}> kErrorCatalogue{{{{",
    ]
    for r in records:
        fields = [
            f'{{.code = ErrorCode::k{r["name"]},',
            f'.name = "{r["name"]}",',
            f'.category = ErrorCategory::k{r["category"].title()},',
            f'.severity = ErrorSeverity::k{r["severity"].title()},',
            f'.retryable = {str(r["retryable"]).lower()},',
            f'.media_error = {r["media_error"]},',
            f'.canonical = {json.dumps(r["canonical"])},',
            f'.description = {json.dumps(r["description"])}}},',
        ]
        single = "    " + " ".join(fields)
        if len(single) <= 80:
            header.append(single)
        else:
            header.append("    " + fields[0])
            header.extend("     " + field for field in fields[1:])
    header += ["}};", "",
               "[[nodiscard]] constexpr const ErrorCatalogueEntry* FindError(ErrorCode code) {",
               "  for (const auto& entry : kErrorCatalogue) {",
               "    if (entry.code == code) {", "      return &entry;", "    }",
               "  }", "  return nullptr;", "}", "", "}", "", "#endif", ""]
    docs = [
        "# Error catalogue", "",
        "Generated from `errors/catalogue.toml` by `tools/errors/generate.py`.",
        "Never renumber, reuse or change the meaning of a released code.",
        "MediaError numbers are metadata for #96, not a mapping implementation.",
        "",
        "| Code | Name | Category | Severity | Retryable | MediaError | Canonical | Example |",
        "|------|------|----------|----------|-----------|------------|-----------|---------|",
    ]
    for r in records:
        docs.append(
            f'| {r["code"]} | {r["name"]} | {r["category"]} | '
            f'{r["severity"]} | {str(r["retryable"]).lower()} | '
            f'{r["media_error"]} | `{r["canonical"]}` | {r["description"]} |')
    ts = ["export enum ErrorCode {"]
    kotlin = ["package org.alumbra", "", "enum class ErrorCode(val code: Int) {"]
    swift = ["public enum ErrorCode: Int {"]
    for r in records:
        ts.append(f'  {r["name"]} = {r["code"]},')
        kotlin.append(f'  {r["name"]}({r["code"]}),')
        swift.append(f'  case {r["name"][0].lower() + r["name"][1:]} = {r["code"]}')
    return {
        "umbra/base/include/umbra/base/error_catalogue.h": "\n".join(header),
        "docs/reference/error-catalogue.md": "\n".join(docs) + "\n",
        "antumbra/generated/ErrorCode.ts": "\n".join(ts + ["}", ""]),
        "antumbra/generated/ErrorCode.kt": "\n".join(kotlin + ["}", ""]),
        "antumbra/generated/ErrorCode.swift": "\n".join(swift + ["}", ""]),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        outputs = render(load(ROOT / "errors/catalogue.toml"))
        stale = []
        for relative, content in outputs.items():
            path = ROOT / relative
            if args.check:
                if not path.is_file() or path.read_text() != content:
                    stale.append(relative)
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
        if stale:
            raise ValueError("stale generated files: " + ", ".join(stale))
    except (ValueError, OSError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
