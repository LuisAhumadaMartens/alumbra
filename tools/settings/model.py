import dataclasses
import pathlib

import validate


@dataclasses.dataclass(frozen=True)
class Setting:
    key: str
    type: str
    default: object
    minimum: object
    maximum: object
    values: tuple
    platforms: tuple
    audience: str
    stability: str
    apply: str
    auto_policy: str
    deprecated: bool
    replacement: str


@dataclasses.dataclass(frozen=True)
class Model:
    release: str
    platforms: tuple
    settings: tuple
    aliases: tuple


def build(directory):
    release, settings, aliases = validate.validate_documents(
        validate.read_directory(pathlib.Path(directory)))
    result = []
    for key in sorted(settings):
        record = settings[key]
        bounds = record.get("range")
        deprecation = record.get("deprecation")
        result.append(Setting(
            key=key,
            type=record["type"],
            default=record["default"],
            minimum=bounds["min"] if bounds else None,
            maximum=bounds["max"] if bounds else None,
            values=tuple(entry["value"] for entry in record.get("values", [])),
            platforms=tuple(validate.META["platforms"].index(name)
                            for name in record["platforms"]),
            audience=record["audience"],
            stability=record["stability"],
            apply=record["apply"],
            auto_policy=record.get("auto", {}).get("policy", ""),
            deprecated=deprecation is not None,
            replacement=(deprecation or {}).get("replacement", "")))
    return Model(
        release=".".join(str(part) for part in release),
        platforms=tuple(validate.META["platforms"]),
        settings=tuple(result),
        aliases=tuple((alias, settings_key) for alias, (settings_key, _)
                      in sorted(aliases.items())))
