import pathlib
import re
import tomllib

import yaml_subset

ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = ROOT / "test/conformance/manifest.yaml"
SCHEMA = 1
KINDS = ("download", "generated", "remote")
PROTOCOLS = ("progressive", "hls", "dash")
OUTCOMES = ("plays", "error")
TRACK_TYPES = ("video", "audio", "text")
REDISTRIBUTABLE_LICENCES = frozenset({
    "Apache-2.0", "BSD-2-Clause", "BSD-3-Clause", "CC-BY-2.5", "CC-BY-3.0",
    "CC-BY-4.0", "CC0-1.0", "MIT"})
ID = re.compile(r"[a-z0-9]+(?:-[a-z0-9]+)*")
TAG = re.compile(r"[a-z0-9]+(?:-[a-z0-9]+)*")
SHA256 = re.compile(r"[0-9a-f]{64}")
FILE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*\.[A-Za-z0-9]+")
SPDX = re.compile(r"(?:LicenseRef-)?[A-Za-z0-9][A-Za-z0-9.+-]*")
CODEC = re.compile(r"[A-Za-z0-9][A-Za-z0-9.+-]*")
AVC = re.compile(r"avc[13]\.[0-9a-fA-F]{6}")
CODEC_PREFIXES = (("avc1.", "h264"), ("avc3.", "h264"), ("hvc1.", "hevc"), ("hev1.", "hevc"),
                  ("mp4a.40.", "aac"), ("vp09.", "vp9"), ("av01.", "av1"))
CODEC_NAMES = {"mp3": "mp3", "opus": "opus", "flac": "flac", "vp8": "vp8", "ac-3": "ac3",
               "ec-3": "eac3", "srt": "subrip", "wvtt": "webvtt"}
CONTAINER = re.compile(r"[a-z0-9]+(?:-[a-z0-9]+)*")
GENERATOR = re.compile(r"test/conformance/generators/[a-z0-9_]+\.sh")
TOP_FIELDS = {"schema", "tags", "entries"}
ENTRY_REQUIRED = {
    "id", "title", "kind", "provenance", "licence", "redistributable",
    "protocol", "container", "codecs", "tags", "platforms", "expected"}
KIND_REQUIRED = {
    "download": {"urls", "sha256", "size", "file"},
    "generated": {"generator", "file"},
    "remote": {"urls"},
}
LICENCE_FIELDS = {"spdx", "url", "evidence", "attribution", "notes"}
EXPECTED_FIELDS = {"outcome", "error", "duration", "tracks"}
TRACK_FIELDS = {
    "video": {"type", "codec", "width", "height", "frame_rate", "frames"},
    "audio": {"type", "codec", "sample_rate", "channels"},
    "text": {"type", "codec", "cues"},
}


def codec_family(codec):
    for prefix, family in CODEC_PREFIXES:
        if codec.startswith(prefix):
            return family
    return CODEC_NAMES.get(codec)


class ManifestError(ValueError):
    pass


class Problems:
    def __init__(self):
        self.messages = []

    def add(self, where, message):
        self.messages.append(f"{where}: {message}")

    def raise_if_any(self):
        if self.messages:
            raise ManifestError("\n".join(self.messages))


def _text(value):
    return isinstance(value, str) and bool(value.strip()) and value == value.strip()


def _integer(value, minimum):
    return type(value) is int and value >= minimum


def _https(value):
    return isinstance(value, str) and re.fullmatch(r"https://[^\s/]+/\S*", value) is not None


def _string_list(value, pattern=None):
    return (isinstance(value, list)
            and all(isinstance(item, str) for item in value)
            and len(value) == len(set(value))
            and (pattern is None or all(pattern.fullmatch(item) for item in value)))


def _fields(problems, where, record, required, optional=frozenset()):
    if not isinstance(record, dict):
        problems.add(where, "expected a mapping")
        return False
    missing = sorted(set(required) - record.keys())
    unknown = sorted(record.keys() - set(required) - set(optional))
    if missing:
        problems.add(where, f"missing fields: {', '.join(missing)}")
    if unknown:
        problems.add(where, f"unknown fields: {', '.join(unknown)}")
    return not missing and not unknown


def _licence(problems, where, entry):
    licence = entry["licence"]
    if not _fields(problems, where + ".licence", licence, {"url", "evidence"},
                   LICENCE_FIELDS - {"url", "evidence"}):
        return
    spdx = licence.get("spdx")
    if spdx is not None and not (isinstance(spdx, str) and SPDX.fullmatch(spdx)):
        problems.add(where, "licence.spdx must be an SPDX identifier or LicenseRef-*")
    for field in ("url", "evidence"):
        if not _https(licence[field]):
            problems.add(where, f"licence.{field} must be an https URL")
    for field in ("attribution", "notes"):
        if field in licence and not _text(licence[field]):
            problems.add(where, f"licence.{field} must be nonempty text")
    third_party = entry["kind"] != "generated"
    if (third_party or str(spdx).startswith("CC-BY")) and "attribution" not in licence:
        problems.add(where, "third-party and CC BY entries need licence.attribution")
    if entry["redistributable"] is True and spdx not in REDISTRIBUTABLE_LICENCES:
        problems.add(where, "redistributable entries need an SPDX licence that permits "
                            f"redistribution, one of {', '.join(sorted(REDISTRIBUTABLE_LICENCES))}")


def _duration(problems, where, duration):
    if not _fields(problems, where + ".expected.duration", duration, {"value", "timescale"}):
        return
    if not _integer(duration["value"], 1) or not _integer(duration["timescale"], 1):
        problems.add(where, "expected.duration value and timescale must be positive integers")


def _tracks(problems, where, tracks, codecs):
    if not isinstance(tracks, list) or not tracks:
        problems.add(where, "expected.tracks must be a nonempty list")
        return
    for index, track in enumerate(tracks):
        place = f"{where}.expected.tracks[{index}]"
        kind = track.get("type") if isinstance(track, dict) else None
        if kind not in TRACK_TYPES:
            problems.add(place, f"type must be one of {', '.join(TRACK_TYPES)}")
            continue
        if not _fields(problems, place, track, {"type", "codec"},
                       TRACK_FIELDS[kind] - {"type", "codec"}):
            continue
        if track["codec"] not in codecs:
            problems.add(place, f"codec {track['codec']!r} is not listed in codecs")
        for field in TRACK_FIELDS[kind] - {"type", "codec"}:
            if field in track and not _integer(track[field], 1):
                problems.add(place, f"{field} must be a positive integer")
    listed = {track.get("codec") for track in tracks if isinstance(track, dict)}
    for codec in codecs:
        if codec not in listed:
            problems.add(where, f"codec {codec!r} has no expected track")


def _expected(problems, where, entry, codecs):
    expected = entry["expected"]
    if not _fields(problems, where + ".expected", expected, {"outcome"},
                   EXPECTED_FIELDS - {"outcome"}):
        return
    outcome = expected["outcome"]
    if outcome not in OUTCOMES:
        problems.add(where, f"expected.outcome must be one of {', '.join(OUTCOMES)}")
    elif (outcome == "error") != ("error" in expected):
        problems.add(where, "expected.error is required exactly when the outcome is error")
    if "error" in expected and not (isinstance(expected["error"], str)
                                    and re.fullmatch(r"[A-Z][A-Za-z0-9]*", expected["error"])):
        problems.add(where, "expected.error must be an error catalogue name")
    if "duration" in expected:
        if "live" in entry["tags"]:
            problems.add(where, "live entries cannot declare a fixed duration")
        _duration(problems, where, expected["duration"])
    if "tracks" in expected and codecs is not None:
        _tracks(problems, where, expected["tracks"], codecs)


def _source(problems, where, entry):
    kind = entry["kind"]
    if "urls" in entry:
        urls = entry["urls"]
        if not _string_list(urls) or not urls or not all(_https(url) for url in urls):
            problems.add(where, "urls must be a nonempty list of distinct https URLs")
    if kind == "download":
        if not (isinstance(entry["sha256"], str) and SHA256.fullmatch(entry["sha256"])):
            problems.add(where, "sha256 must be 64 lowercase hexadecimal digits")
        if not _integer(entry["size"], 1):
            problems.add(where, "size must be a positive integer number of bytes")
        if entry["redistributable"] is not True:
            problems.add(where, "downloaded copies need verified redistribution rights")
    if "file" in entry and not (isinstance(entry["file"], str) and FILE.fullmatch(entry["file"])):
        problems.add(where, "file must be a plain file name with an extension")
    if kind == "generated":
        generator = entry["generator"]
        if not (isinstance(generator, str) and GENERATOR.fullmatch(generator)):
            problems.add(where, "generator must be a script in test/conformance/generators/")
        elif not (ROOT / generator).is_file():
            problems.add(where, f"generator {generator} does not exist")


def _tag_rules(problems, where, entry, vocabulary):
    tags = entry["tags"]
    if not _string_list(tags, TAG) or not tags:
        problems.add(where, "tags must be a nonempty list of distinct lowercase tags")
        return
    unknown = [tag for tag in tags if tag not in vocabulary]
    if unknown:
        problems.add(where, f"tags not declared in the vocabulary: {', '.join(unknown)}")
    if entry["protocol"] not in tags:
        problems.add(where, f"tags must include the protocol {entry['protocol']!r}")
    for kind in ("generated", "remote"):
        if (entry["kind"] == kind) != (kind in tags):
            problems.add(where, f"the {kind!r} tag is required exactly for {kind} entries")
    if "live" in tags and entry["kind"] != "remote":
        problems.add(where, "live entries must be remote until a local live generator exists")


def _entry(problems, where, entry, vocabulary, platforms):
    kind = entry.get("kind") if isinstance(entry, dict) else None
    if kind not in KINDS:
        problems.add(where, f"kind must be one of {', '.join(KINDS)}")
        return
    if not _fields(problems, where, entry, ENTRY_REQUIRED | KIND_REQUIRED[kind]):
        return
    for field in ("title", "provenance"):
        if not _text(entry[field]):
            problems.add(where, f"{field} must be nonempty text")
    if type(entry["redistributable"]) is not bool:
        problems.add(where, "redistributable must be true or false")
    if entry["protocol"] not in PROTOCOLS:
        problems.add(where, f"protocol must be one of {', '.join(PROTOCOLS)}")
    if not (isinstance(entry["container"], str) and CONTAINER.fullmatch(entry["container"])):
        problems.add(where, "container must be a lowercase container name")
    codecs = entry["codecs"]
    if not _string_list(codecs, CODEC) or not codecs:
        problems.add(where, "codecs must be a nonempty list of distinct codec strings")
        codecs = None
    elif not all(AVC.fullmatch(codec) for codec in codecs if codec.startswith(("avc1", "avc3"))):
        problems.add(where, "AVC codecs must be avc1 or avc3 with six hexadecimal digits")
        codecs = None
    elif kind != "remote" and any(codec_family(codec) is None for codec in codecs):
        unknown = ", ".join(codec for codec in codecs if codec_family(codec) is None)
        problems.add(where, f"codecs the media check cannot verify: {unknown}; "
                            "add them to manifest.CODEC_NAMES or CODEC_PREFIXES")
        codecs = None
    if not _string_list(entry["platforms"]) or not entry["platforms"]:
        problems.add(where, "platforms must be a nonempty list of distinct platforms")
    elif platforms is not None and not set(entry["platforms"]) <= set(platforms):
        problems.add(where, f"platforms must be drawn from {', '.join(platforms)}")
    _source(problems, where, entry)
    _licence(problems, where, entry)
    _tag_rules(problems, where, entry, vocabulary)
    if isinstance(entry["tags"], list):
        _expected(problems, where, entry, codecs)


def validate(document, platforms=None, errors=None):
    problems = Problems()
    if not _fields(problems, "manifest", document, TOP_FIELDS):
        problems.raise_if_any()
    if type(document["schema"]) is not int or document["schema"] != SCHEMA:
        problems.add("manifest", f"schema must be {SCHEMA}")
    vocabulary = document["tags"]
    if not isinstance(vocabulary, dict) or not vocabulary:
        problems.add("manifest", "tags must map each tag to its description")
        vocabulary = {}
    for tag, description in vocabulary.items():
        if not TAG.fullmatch(tag) or not _text(description):
            problems.add(f"tags.{tag}", "tags must be lowercase words with a description")
    entries = document["entries"]
    if not isinstance(entries, list) or not entries:
        problems.add("manifest", "entries must be a nonempty list")
        problems.raise_if_any()
    seen = set()
    for index, entry in enumerate(entries):
        identifier = entry.get("id") if isinstance(entry, dict) else None
        if not (isinstance(identifier, str) and ID.fullmatch(identifier)):
            problems.add(f"entries[{index}]", "id must be lowercase words joined by hyphens")
            continue
        if identifier in seen:
            problems.add(identifier, "duplicate id")
        seen.add(identifier)
        _entry(problems, identifier, entry, vocabulary, platforms)
        expected = entry.get("expected")
        if (errors is not None and isinstance(expected, dict)
                and isinstance(expected.get("error"), str)
                and expected["error"] not in errors):
            problems.add(identifier, f"expected.error {expected['error']!r} is not in the "
                                     "error catalogue")
    used = {tag for entry in entries
            if isinstance(entry, dict) and isinstance(entry.get("tags"), list)
            for tag in entry["tags"] if isinstance(tag, str)}
    for tag in vocabulary:
        if tag not in used:
            problems.add(f"tags.{tag}", "declared but not used by any entry")
    problems.raise_if_any()
    return document


def reference_vocabularies(root=ROOT):
    meta = tomllib.loads((root / "settings/meta-schema.toml").read_text(encoding="utf-8"))
    catalogue = tomllib.loads((root / "errors/catalogue.toml").read_text(encoding="utf-8"))
    return meta["platforms"], {record["name"] for record in catalogue["error"]}


def load(path=DEFAULT_MANIFEST, root=ROOT):
    path = pathlib.Path(path)
    try:
        document = yaml_subset.load(path.read_text(encoding="utf-8"))
    except yaml_subset.YamlSubsetError as error:
        raise ManifestError(f"{path}: {error}") from error
    platforms, errors = reference_vocabularies(root)
    return validate(document, platforms, errors)


def select(entries, tags=(), ids=(), kinds=()):
    known = {entry["id"] for entry in entries}
    unknown = [identifier for identifier in ids if identifier not in known]
    if unknown:
        raise ManifestError(f"unknown entry ids: {', '.join(unknown)}")
    return [entry for entry in entries
            if set(tags) <= set(entry["tags"])
            and (not ids or entry["id"] in ids)
            and (not kinds or entry["kind"] in kinds)]


def requires_attribution(entry):
    return entry["kind"] != "generated"
