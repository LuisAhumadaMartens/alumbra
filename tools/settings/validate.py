import argparse
import math
import pathlib
import re
import subprocess
import sys
import tomllib


ROOT = pathlib.Path(__file__).resolve().parents[2]
META = tomllib.loads((ROOT / "settings/meta-schema.toml").read_text(encoding="utf-8"))
NAME = re.compile(r"[a-z][a-z0-9]*(?:_[a-z0-9]+)*(?:\.[a-z][a-z0-9]*(?:_[a-z0-9]+)*)+")
RELEASE = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")


class ValidationError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ValidationError(message)


def fields(record, required, optional=()):
    require(isinstance(record, dict), "expected a table")
    require(set(required) <= record.keys(),
            f"missing fields: {sorted(set(required) - record.keys())}")
    require(record.keys() <= set(required) | set(optional),
            f"unknown fields: {sorted(record.keys() - set(required) - set(optional))}")


def text(value):
    return isinstance(value, str) and bool(value.strip())


def version(value):
    require(isinstance(value, str) and RELEASE.fullmatch(value),
            f"invalid release: {value!r}; expected major.minor.patch")
    try:
        return tuple(int(part) for part in value.split("."))
    except ValueError as error:
        raise ValidationError(f"release component exceeds Python's integer parsing limit: {value!r}") from error


def name(value):
    require(isinstance(value, str) and NAME.fullmatch(value),
            f"invalid key name: {value!r}")
    parts = value.split(".")
    require(not set(parts) & set(META["platforms"] + ["desktop", "apple"]),
            f"platform in key name: {value}")
    require(not any({"disable", "disabled"} & set(part.split("_")) for part in parts),
            f"negative key name: {value}")


def string_set(value, allowed=None):
    require(isinstance(value, list) and all(text(item) for item in value),
            "expected a string list")
    require(len(value) == len(set(value)), "duplicate list value")
    if allowed is not None:
        require(set(value) <= set(allowed), f"unknown list value: {value!r}")


def alias_boundary(since):
    major, minor, _ = version(since)
    return major, minor + 2, 0


def removal_release(value, since):
    result = version(value)
    require(result[2] == 0, "removal must start at a release boundary (patch zero)")
    require(result >= alias_boundary(since),
            "alias must remain through the entire next minor release")
    if version(since)[0] >= 1:
        require(result[0] > version(since)[0] and result[1] == 0,
                "after 1.0 removal requires a new major release")
    return result


def validate_setting(record, release):
    fields(record, META["required_fields"], META["optional_fields"])
    name(record["key"])
    kind = record["type"]
    require(isinstance(kind, str) and kind in META["types"], f"unknown type: {kind!r}")
    require(text(record["description"]), "description must be nonempty Markdown")
    require(record["unit"] in META["units"], "unknown unit")
    string_set(record["platforms"], META["platforms"])
    require(bool(record["platforms"]), "platforms must not be empty")
    for field, vocabulary in (("audience", "audiences"), ("stability", "stabilities"),
                              ("apply", "apply_modes")):
        require(record[field] in META[vocabulary], f"unknown {field}: {record[field]!r}")
    require(version(record["since"]) <= release, "key since is after schema release")
    leaf = record["key"].split(".")[-1]
    suffix = next((unit for unit in META["units"] if unit and leaf.endswith("_" + unit)), "")
    require(suffix == record["unit"], "unit does not match key suffix")
    expected_unit = {"duration": "ms", "byte_size": "bytes"}.get(kind)
    if expected_unit:
        require(record["unit"] == expected_unit, f"{kind} requires unit {expected_unit}")
    elif kind not in {"integer", "double"}:
        require(record["unit"] == "", f"{kind} cannot have a unit")
    value = record["default"]
    numeric = kind in {"integer", "double", "duration", "byte_size"}
    if kind == "boolean":
        require(type(value) is bool, "boolean default must be a boolean")
    elif kind in {"integer", "duration", "byte_size"}:
        require(type(value) is int, f"{kind} default must be an integer")
    elif kind == "double":
        require(type(value) is float and math.isfinite(value),
                "double default must be a finite TOML float")
    elif kind in {"string", "enum"}:
        require(isinstance(value, str), f"{kind} default must be a string")
    elif kind == "string_list":
        string_set(value)
    if numeric:
        require("range" in record, "numeric setting requires a range")
        bounds = record["range"]
        fields(bounds, ["min", "max"])
        for bound in bounds.values():
            if kind == "double":
                require((type(bound) is float and math.isfinite(bound))
                        or (type(bound) is int and abs(bound) <= sys.float_info.max),
                        "double bounds must be finite numbers")
            else:
                require(type(bound) is int and META["integer_min"] <= bound <= META["integer_max"],
                        "integer bounds must fit signed 64 bits")
        require(bounds["min"] <= value <= bounds["max"], "default outside range")
        if kind in {"duration", "byte_size"}:
            require(bounds["min"] >= 0, f"{kind} must be nonnegative")
    else:
        require("range" not in record, f"range is not valid for {kind}")
    if "auto" in record:
        auto = record["auto"]
        fields(auto, ["value", "policy"])
        require(type(auto["value"]) is int and auto["value"] == 0 and value == 0,
                "auto must reserve integer zero and use it as the default")
        require((kind == "integer" and record["unit"] == "" and record["range"]["min"] == 0
                 and auto["policy"] == "hardware_concurrency_budget")
                or (kind == "duration" and auto["policy"] == "half_frame_interval_capped"
                    and record["range"] == {"min": 0, "max": 8}),
                "auto policy does not match setting type, unit or range")
    if kind == "enum":
        require("values" in record, "enum requires documented values")
    if "values" in record:
        require(kind in {"enum", "string", "string_list"}, "values require a string type")
        entries = record["values"]
        require(isinstance(entries, list) and bool(entries), "values must be a nonempty list")
        for entry in entries:
            fields(entry, ["value", "description"])
            require(text(entry["value"]) and text(entry["description"]),
                    "each allowed value needs a value and description")
        allowed = [entry["value"] for entry in entries]
        string_set(allowed)
        require(set(value if kind == "string_list" else [value]) <= set(allowed),
                "default outside allowed values")
    aliases = record.get("aliases", [])
    require(isinstance(aliases, list), "aliases must be a list")
    for alias in aliases:
        fields(alias, ["key", "since"], ["remove_in"])
        name(alias["key"])
        alias_leaf = alias["key"].split(".")[-1]
        alias_unit = next((unit for unit in META["units"]
                           if unit and alias_leaf.endswith("_" + unit)), "")
        require(alias_unit == record["unit"], "alias unit does not match target unit")
        introduced = version(alias["since"])
        require(version(record["since"]) <= introduced <= release,
                "alias since must be between key since and schema release")
        if "remove_in" in alias:
            require(removal_release(alias["remove_in"], alias["since"]) > release,
                    "expired alias must be removed from this release")
    if "deprecation" in record:
        deprecation = record["deprecation"]
        fields(deprecation, ["since"], ["replacement"])
        require(version(record["since"]) <= version(deprecation["since"]) <= release,
                "deprecation since must be between key since and schema release")
        if "replacement" in deprecation:
            name(deprecation["replacement"])


def validate_documents(documents):
    settings = {}
    aliases = {}
    releases = set()
    require(bool(documents), "no schema files found")
    for path, source in documents:
        try:
            document = tomllib.loads(source)
            fields(document, META["root_fields"])
            require(type(document["schema_version"]) is int
                    and document["schema_version"] == META["schema_version"],
                    "unsupported schema_version")
            release = version(document["release"])
            releases.add(release)
            records = document["settings"]
            require(isinstance(records, list) and bool(records), "settings must be a nonempty list")
            for record in records:
                try:
                    validate_setting(record, release)
                except ValidationError as error:
                    key = record.get("key", "<unnamed>") if isinstance(record, dict) else "<invalid>"
                    raise ValidationError(f"{key}: {error}") from error
                key = record["key"]
                require(key.split(".")[0] == pathlib.Path(path).stem,
                        f"{key}: namespace must match file name")
                require(key not in settings, f"duplicate key: {key}")
                settings[key] = record
                for alias in record.get("aliases", []):
                    require(alias["key"] not in aliases, f"duplicate alias: {alias['key']}")
                    aliases[alias["key"]] = (key, alias)
        except (tomllib.TOMLDecodeError, ValidationError) as error:
            raise ValidationError(f"{path}: {error}") from error
    require(len(releases) == 1, "schema files must have the same release")
    require(not settings.keys() & aliases.keys(), "alias collides with a canonical key")
    for key, record in settings.items():
        replacement = record.get("deprecation", {}).get("replacement")
        if replacement is not None:
            require(replacement in settings and replacement != key,
                    f"{key}: replacement must be another canonical key")
            require(settings[replacement]["type"] == record["type"],
                    f"{key}: replacement has a different type")
            require("deprecation" not in settings[replacement],
                    f"{key}: replacement must not itself be deprecated")
    return releases.pop(), settings, aliases


def validate_history(current, previous):
    release, settings, aliases = current
    old_release, old_settings, old_aliases = previous
    require(release >= old_release, "schema release must not go backwards")
    for key, (target, alias) in old_aliases.items():
        if key in aliases:
            new_target, new_alias = aliases[key]
            require(new_target == target and new_alias["since"] == alias["since"],
                    f"{key}: alias target and since are immutable")
            if "remove_in" in alias:
                require("remove_in" in new_alias
                        and version(new_alias["remove_in"]) >= version(alias["remove_in"]),
                        f"{key}: cannot shorten an announced alias window")
        else:
            boundary = removal_release(alias["remove_in"], alias["since"]) if "remove_in" in alias else alias_boundary(alias["since"])
            require(release >= boundary, f"{key}: alias removed before compatibility window")
            if old_release[0] >= 1:
                require(release[0] > old_release[0],
                        f"{key}: alias removal after 1.0 requires a major release")
            require(key not in settings, f"{key}: cannot reuse an alias as a canonical key")
    for key, record in old_settings.items():
        if key in settings:
            require(settings[key]["since"] == record["since"], f"{key}: since is immutable")
            require(settings[key]["type"] == record["type"], f"{key}: type is immutable")
            if "deprecation" in record:
                require(settings[key].get("deprecation", {}).get("since")
                        == record["deprecation"]["since"],
                        f"{key}: deprecation start is immutable")
        elif key not in aliases:
            if old_release[0] >= 1:
                require(release[0] > old_release[0], f"{key}: removal requires a major release")
        elif key in aliases:
            require(settings[aliases[key][0]]["type"] == record["type"],
                    f"{key}: renamed key must retain its type")


def read_directory(directory):
    return [(path.name, path.read_text(encoding="utf-8"))
            for path in sorted(directory.glob("*.toml"))]


def read_ref(ref):
    result = subprocess.run(["git", "ls-tree", "-r", "--name-only", ref, "--",
                             "settings/schema"], cwd=ROOT, capture_output=True, text=True)
    require(result.returncode == 0, f"cannot read previous ref: {result.stderr.strip()}")
    documents = []
    for path in result.stdout.splitlines():
        if not path.endswith(".toml"):
            continue
        content = subprocess.run(["git", "show", f"{ref}:{path}"], cwd=ROOT,
                                 capture_output=True, text=True)
        require(content.returncode == 0, f"cannot read previous schema: {content.stderr.strip()}")
        documents.append((path, content.stdout))
    return documents


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", nargs="?", type=pathlib.Path, default=ROOT / "settings/schema")
    parser.add_argument("--previous", type=pathlib.Path)
    parser.add_argument("--previous-ref")
    args = parser.parse_args()
    try:
        require(not (args.previous and args.previous_ref), "choose only one previous schema source")
        current = validate_documents(read_directory(args.directory))
        previous = read_directory(args.previous) if args.previous else (
            read_ref(args.previous_ref) if args.previous_ref else None)
        if previous:
            validate_history(current, validate_documents(previous))
        elif args.previous:
            raise ValidationError("previous schema directory has no TOML files")
    except (ValidationError, OSError, UnicodeError) as error:
        print(f"settings validation failed: {error}", file=sys.stderr)
        return 1
    print(f"Validated {len(current[1])} settings and {len(current[2])} aliases")
    return 0


if __name__ == "__main__":
    sys.exit(main())
