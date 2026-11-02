import argparse
import json
from pathlib import Path
import re
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[2]
SOURCE = "decoders/blocklist.toml"
TABLE = "penumbra/reference_player/include/penumbra/reference_player/decoder_blocklist_table.h"
PAGE = "docs/reference/decoder-blocklist.md"
CODECS = ("h264", "hevc", "vp9", "av1", "aac", "opus", "ac3", "eac3", "flac",
          "mp3")
REQUIRED = ("id", "decoder", "reason", "evidence", "since")
MATCH_TEXT = ("codec", "port", "device_model", "gpu_vendor", "gpu_device")
RANGES = (("os_min", "os_max"), ("driver_min", "driver_max"))
FIELDS = set(REQUIRED) | set(MATCH_TEXT) | {"os_min", "os_max", "driver_min",
                                            "driver_max"}
ID = re.compile(r"[a-z][a-z0-9]*(-[a-z0-9]+)*")
VERSION = re.compile(r"[0-9]+(\.[0-9]+)*")
EVIDENCE = re.compile(r"https://[A-Za-z0-9._~:/?#@!$&'()*+,;=%-]+")
TEXT = re.compile(r"[A-Za-z0-9 ,().:/_'+-]+")


def version_key(text):
    return tuple(int(part) for part in text.split("."))


def check_text(record, field):
    value = record[field]
    if not isinstance(value, str) or not TEXT.fullmatch(value):
        raise ValueError(f"{record.get('id')}: {field} must be plain text")


def load(path):
    with path.open("rb") as stream:
        source = tomllib.load(stream)
    if (not set(source) <= {"version", "entry"}
            or type(source.get("version")) is not int
            or source["version"] != 1):
        raise ValueError("blocklist requires version 1 and entry records")
    records = source.get("entry", [])
    if not isinstance(records, list):
        raise ValueError("entry must be an array of tables")
    seen = set()
    for record in records:
        if not isinstance(record, dict) or not set(record) <= FIELDS:
            raise ValueError("entry has unknown fields")
        missing = [field for field in REQUIRED if field not in record]
        if missing:
            raise ValueError(f"entry is missing {', '.join(missing)}")
        for field in record:
            if not isinstance(record[field], str):
                raise ValueError(f"{record['id']}: {field} must be a string")
        if not ID.fullmatch(record["id"]) or record["id"] in seen:
            raise ValueError(f"entry IDs must be unique kebab-case: {record['id']}")
        seen.add(record["id"])
        for field in record:
            if field not in ("id", "evidence"):
                check_text(record, field)
        if not record["reason"].strip() or not record["decoder"].strip():
            raise ValueError(f"{record['id']}: decoder and reason are required")
        if not EVIDENCE.fullmatch(record["evidence"]):
            raise ValueError(f"{record['id']}: evidence must be an https link")
        if "codec" in record and record["codec"] not in CODECS:
            raise ValueError(f"{record['id']}: unknown codec")
        for field in ("since", "os_min", "os_max", "driver_min", "driver_max"):
            if field in record and not VERSION.fullmatch(record[field]):
                raise ValueError(f"{record['id']}: {field} must be dotted digits")
        for low, high in RANGES:
            if (low in record and high in record
                    and version_key(record[low]) > version_key(record[high])):
                raise ValueError(f"{record['id']}: {low} exceeds {high}")
    return sorted(records, key=lambda record: record["id"])


def render(records):
    fields = ("id", "decoder", "codec", "port", "os_min", "os_max",
              "device_model", "gpu_vendor", "gpu_device", "driver_min",
              "driver_max", "reason", "evidence", "since")
    header = [
        "#ifndef PENUMBRA_REFERENCE_PLAYER_DECODER_BLOCKLIST_TABLE_H_",
        "#define PENUMBRA_REFERENCE_PLAYER_DECODER_BLOCKLIST_TABLE_H_", "",
        "#include <array>", "",
        '#include "penumbra/reference_player/decoder_blocklist.h"', "",
        "namespace penumbra {", "",
    ]
    if records:
        header.append(
            f"inline constexpr std::array<BlocklistEntry, {len(records)}> "
            "kDecoderBlocklist{{")
        for record in records:
            header.append("    {")
            for field in fields:
                if field in record:
                    header.append(
                        f"        .{field} = {json.dumps(record[field])},")
            header.append("    },")
        header.append("}};")
    else:
        header.append(
            "inline constexpr std::array<BlocklistEntry, 0> "
            "kDecoderBlocklist{};")
    header += ["", "}", "", "#endif", ""]
    page = [
        "# Decoder blocklist", "",
        f"Generated from `{SOURCE}` by `tools/decoders/generate_blocklist.py`.",
        "Each entry removes one decoder from the candidate list of the "
        "reference player when the device matches. Empty match fields match "
        "everything.", "",
    ]
    if records:
        page += [
            "| ID | Decoder | Codec | Port | OS | Device | GPU | Driver | Reason | Evidence | Since |",
            "|----|---------|-------|------|----|--------|-----|--------|--------|----------|-------|",
        ]
        for record in records:
            def span(low, high):
                return f"{record.get(low, '')}..{record.get(high, '')}" \
                    if low in record or high in record else ""
            gpu = " ".join(record[f] for f in ("gpu_vendor", "gpu_device")
                           if f in record)
            page.append(
                f"| `{record['id']}` | `{record['decoder']}` | "
                f"{record.get('codec', '')} | {record.get('port', '')} | "
                f"{span('os_min', 'os_max')} | "
                f"{record.get('device_model', '')} | {gpu} | "
                f"{span('driver_min', 'driver_max')} | {record['reason']} | "
                f"{record['evidence']} | {record['since']} |")
    else:
        page.append("No decoder is blocklisted yet.")
    return {TABLE: "\n".join(header), PAGE: "\n".join(page) + "\n"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--source", type=Path, default=ROOT / SOURCE)
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()
    try:
        outputs = render(load(args.source))
        stale = []
        for relative, content in outputs.items():
            path = args.root / relative
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
