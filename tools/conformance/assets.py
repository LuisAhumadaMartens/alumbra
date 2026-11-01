#!/usr/bin/env python3
import argparse
import hashlib
import http.client
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

import manifest

USER_AGENT = "alumbra-conformance-fetcher/1"
CHUNK = 1 << 20
PROBE_LIMIT = 1 << 20
RETRYABLE_STATUS = frozenset({408, 425, 429, 500, 502, 503, 504})
MAX_BACKOFF = 30.0
UNAVAILABLE = 77
ATTRIBUTION = manifest.ROOT / "test/conformance/ATTRIBUTION.md"
TRUE = frozenset({"1", "true", "yes", "on"})
AVC_PROFILES = {0x42: {"Baseline", "Constrained Baseline"}, 0x4D: {"Main"}, 0x64: {"High"}}
STREAM_TYPES = {"video": "video", "audio": "audio", "text": "subtitle"}
USES = {"download": "downloaded into the local cache and verified by SHA-256",
        "remote": "streamed from the publisher at test time; never cached or mirrored"}


class AssetError(Exception):
    pass


class Unavailable(AssetError):
    pass


class HttpStatus(AssetError):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


class ToolError(AssetError):
    pass


class MediaMismatch(AssetError):
    pass


class SecureRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        old = urllib.parse.urlsplit(req.full_url).scheme
        new = urllib.parse.urlsplit(newurl).scheme
        if old == "https" and new != "https":
            raise urllib.error.HTTPError(
                newurl, code, f"refusing a redirect from https to {new}", headers, fp)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def offline_from(environ):
    return environ.get("ALUMBRA_ASSET_OFFLINE", "").strip().lower() in TRUE


def default_cache(environ):
    configured = environ.get("ALUMBRA_ASSET_CACHE", "")
    if configured:
        return pathlib.Path(configured).expanduser()
    base = environ.get("XDG_CACHE_HOME", "") or pathlib.Path.home() / ".cache"
    return pathlib.Path(base).expanduser() / "alumbra" / "assets"


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(CHUNK), b""):
            digest.update(chunk)
    return digest.hexdigest()


class Network:
    def __init__(self, opener=None, sleep=time.sleep, attempts=3, backoff=1.0,
                 timeout=60.0, offline=False, log=None):
        self.opener = opener or urllib.request.build_opener(SecureRedirects())
        self.sleep = sleep
        self.attempts = attempts
        self.backoff = backoff
        self.timeout = timeout
        self.offline = offline
        self.log = log or (lambda message: print(message, file=sys.stderr))
        self.requests = 0

    def _call(self, url, method, consume, headers=None):
        if self.offline:
            raise Unavailable("offline mode forbids network access")
        request = urllib.request.Request(
            url, method=method, headers={"User-Agent": USER_AGENT, **(headers or {})})
        for attempt in range(1, self.attempts + 1):
            self.requests += 1
            try:
                with self.opener.open(request, timeout=self.timeout) as response:
                    return consume(response)
            except urllib.error.HTTPError as error:
                error.close()
                if error.code not in RETRYABLE_STATUS:
                    raise HttpStatus(error.code, f"HTTP {error.code} {error.reason}") from error
                reason = f"HTTP {error.code} {error.reason}"
            except (urllib.error.URLError, http.client.HTTPException, OSError) as error:
                reason = str(getattr(error, "reason", error)) or type(error).__name__
            if attempt == self.attempts:
                raise Unavailable(f"{reason} after {self.attempts} attempts")
            delay = min(self.backoff * 2 ** (attempt - 1), MAX_BACKOFF)
            self.log(f"{url}: {reason}; retrying in {delay:g} s")
            self.sleep(delay)
        raise AssertionError("unreachable")

    def download(self, url, destination, size, sha256):
        def local(operation, *arguments):
            try:
                return operation(*arguments)
            except OSError as error:
                raise AssetError(f"cannot write {destination}: {error}") from error

        def consume(response):
            digest = hashlib.sha256()
            received = 0
            with local(open, destination, "wb") as stream:
                for chunk in iter(lambda: response.read(CHUNK), b""):
                    received += len(chunk)
                    if received > size:
                        raise AssetError(f"size mismatch: more than {size} bytes")
                    digest.update(chunk)
                    local(stream.write, chunk)
                local(stream.flush)
            if received != size:
                raise AssetError(f"size mismatch: expected {size} bytes, got {received}")
            actual = digest.hexdigest()
            if actual != sha256:
                raise AssetError(f"SHA-256 mismatch: expected {sha256}, got {actual}")
        self._call(url, "GET", consume)

    def prefix(self, url):
        return self._call(url, "GET", lambda response: response.read(PROBE_LIMIT))

    def length(self, url):
        def total(response):
            content_range = response.headers.get("Content-Range", "")
            if response.status == 206 and "/" in content_range:
                return content_range.rsplit("/", 1)[1]
            return response.headers.get("Content-Length")
        try:
            length = self._call(url, "HEAD", total)
        except HttpStatus as error:
            if error.code not in (405, 501):
                raise
            length = None
        if length is None:
            length = self._call(url, "GET", total, {"Range": "bytes=0-0"})
        return length


class Cache:
    def __init__(self, root):
        self.root = pathlib.Path(root)

    def download_path(self, entry):
        return self.root / "sha256" / entry["sha256"] / entry["file"]

    def generated_path(self, entry, root=manifest.ROOT):
        script = (root / entry["generator"]).read_bytes()
        recipe = hashlib.sha256(
            b"\0".join([b"alumbra-generated-1", entry["id"].encode(), script])).hexdigest()
        return self.root / "generated" / entry["id"] / recipe / entry["file"]

    def scratch(self):
        parent = self.root / "tmp"
        parent.mkdir(parents=True, exist_ok=True)
        return pathlib.Path(tempfile.mkdtemp(dir=parent))


def _stream_problems(track, stream):
    problems = []
    if stream.get("codec_type") != STREAM_TYPES[track["type"]]:
        return [f"expected a {track['type']} track, found {stream.get('codec_type')}"]
    family = manifest.codec_family(track["codec"])
    if stream.get("codec_name") != family:
        problems.append(f"{track['codec']} expects {family}, found {stream.get('codec_name')}")
    if track["codec"].startswith(("avc1.", "avc3.")) and family == stream.get("codec_name"):
        profile, level = int(track["codec"][5:7], 16), int(track["codec"][9:11], 16)
        if stream.get("profile") not in AVC_PROFILES.get(profile, set()):
            problems.append(f"{track['codec']} profile does not match {stream.get('profile')}")
        if stream.get("level") != level:
            problems.append(f"{track['codec']} level does not match {stream.get('level')}")
    checks = (("width", "width"), ("height", "height"), ("sample_rate", "sample_rate"),
              ("channels", "channels"), ("frames", "nb_read_packets"),
              ("cues", "nb_read_packets"))
    for field, key in checks:
        if field in track and str(stream.get(key)) != str(track[field]):
            problems.append(f"{field} {track[field]} != {stream.get(key)}")
    if "frame_rate" in track and stream.get("r_frame_rate") != f"{track['frame_rate']}/1":
        problems.append(f"frame_rate {track['frame_rate']} != {stream.get('r_frame_rate')}")
    return problems


def media_problems(entry, info):
    problems = []
    names = str(info.get("format", {}).get("format_name", "")).split(",")
    if entry["container"] not in names:
        problems.append(f"container {entry['container']} not in {','.join(names)}")
    tracks = entry["expected"].get("tracks", [])
    streams = info.get("streams", [])
    if len(streams) != len(tracks):
        problems.append(f"expected {len(tracks)} tracks, found {len(streams)}")
    for index, (track, stream) in enumerate(zip(tracks, streams)):
        problems.extend(f"track {index}: {problem}" for problem in _stream_problems(track, stream))
    duration = entry["expected"].get("duration")
    if duration is not None:
        expected = duration["value"] / duration["timescale"]
        try:
            actual = float(info["format"]["duration"])
        except (KeyError, TypeError, ValueError):
            actual = None
        if actual is None or abs(actual - expected) > 0.001:
            problems.append(f"duration {expected:.6f} s != {actual}")
    return problems


def ffprobe(path, environ, run=subprocess.run):
    command = [environ.get("FFPROBE", "ffprobe"), "-v", "error", "-count_packets",
               "-show_format", "-show_streams", "-of", "json", str(path)]
    try:
        result = run(command, capture_output=True, text=True, check=False)
    except FileNotFoundError as error:
        raise ToolError(f"{command[0]} is required to verify media") from error
    if result.returncode != 0:
        raise ToolError(f"ffprobe failed: {result.stderr.strip()}")
    try:
        return json.loads(result.stdout)
    except ValueError as error:
        raise ToolError(f"ffprobe returned invalid JSON: {error}") from error


class Fetcher:
    def __init__(self, cache, network, environ=None, run=subprocess.run, root=manifest.ROOT,
                 log=None):
        self.cache = cache
        self.network = network
        self.environ = dict(os.environ if environ is None else environ)
        self.run = run
        self.root = root
        self.log = log or (lambda message: print(message))

    def verify_media(self, entry, path):
        problems = media_problems(entry, ffprobe(path, self.environ, self.run))
        if problems:
            raise MediaMismatch(
                f"{path.name} does not match the manifest: " + "; ".join(problems))

    def _cached_download(self, entry, path):
        actual = sha256_file(path)
        if actual == entry["sha256"] and path.stat().st_size == entry["size"]:
            return True
        self.log(f"{entry['id']}: cached copy is corrupt (SHA-256 {actual}); removing it")
        path.unlink()
        return False

    def _download(self, entry):
        path = self.cache.download_path(entry)
        if path.is_file() and self._cached_download(entry, path):
            return path, "cached"
        if self.network.offline:
            raise AssetError("not in the cache, and offline mode forbids downloading it")
        reasons = []
        unavailable = True
        scratch = self.cache.scratch()
        try:
            for url in entry["urls"]:
                partial = scratch / entry["file"]
                try:
                    self.network.download(url, partial, entry["size"], entry["sha256"])
                except Unavailable as error:
                    reasons.append(f"{url}: {error}")
                    continue
                except AssetError as error:
                    unavailable = False
                    reasons.append(f"{url}: {error}")
                    continue
                path.parent.mkdir(parents=True, exist_ok=True)
                os.replace(partial, path)
                return path, "downloaded"
        finally:
            shutil.rmtree(scratch, ignore_errors=True)
        message = "no source returned a verified copy: " + "; ".join(reasons)
        raise (Unavailable if unavailable else AssetError)(message)

    def _cached_generated(self, entry, path):
        try:
            self.verify_media(entry, path)
            return True
        except MediaMismatch as error:
            self.log(f"{entry['id']}: cached clip is invalid ({error}); regenerating it")
            path.unlink()
            return False

    def _generate(self, entry):
        path = self.cache.generated_path(entry, self.root)
        if path.is_file() and self._cached_generated(entry, path):
            return path, "cached"
        scratch = self.cache.scratch()
        try:
            output = scratch / entry["file"]
            script = self.root / entry["generator"]
            try:
                result = self.run(["bash", str(script), str(output)], capture_output=True,
                                  text=True, check=False, env=self.environ)
            except FileNotFoundError as error:
                raise AssetError("bash is required to run generators") from error
            if result.returncode != 0 or not output.is_file():
                raise AssetError(f"{entry['generator']} failed: "
                                 f"{result.stderr.strip() or 'no output file'}")
            self.verify_media(entry, output)
            path.parent.mkdir(parents=True, exist_ok=True)
            os.replace(output, path)
        finally:
            shutil.rmtree(scratch, ignore_errors=True)
        return path, "generated"

    def fetch(self, entry, probe=False):
        if entry["kind"] == "remote":
            return None, "remote; streamed at test time, so use 'probe' to check it"
        if entry["kind"] == "generated":
            return self._generate(entry)
        path, state = self._download(entry)
        if probe:
            self.verify_media(entry, path)
        return path, state

    def path(self, entry):
        if entry["kind"] == "remote":
            return entry["urls"][0]
        if entry["kind"] == "generated":
            path = self.cache.generated_path(entry, self.root)
        else:
            path = self.cache.download_path(entry)
        if not path.is_file():
            raise AssetError("not in the cache; run 'fetch' first")
        return path

    def probe(self, entry):
        if entry["kind"] == "generated":
            return "generated locally; nothing to probe"
        failures, outages = [], []
        for url in entry["urls"]:
            try:
                if entry["kind"] == "download":
                    length = self.network.length(url)
                    if length != str(entry["size"]):
                        failures.append(f"{url}: Content-Length {length}, expected {entry['size']}")
                    continue
                body = self.network.prefix(url).lstrip(b"\xef\xbb\xbf")
            except Unavailable as error:
                outages.append(f"{url}: {error}")
                continue
            except AssetError as error:
                failures.append(f"{url}: {error}")
                continue
            if entry["protocol"] == "hls" and not body.startswith(b"#EXTM3U"):
                failures.append(f"{url}: not an HLS playlist")
            if entry["protocol"] == "dash" and b"<MPD" not in body:
                failures.append(f"{url}: not a DASH MPD")
        if failures:
            raise AssetError("; ".join(failures + outages))
        if outages:
            raise Unavailable("; ".join(outages))
        return f"{len(entry['urls'])} source(s) reachable"


def render_attribution(entries):
    def section(entry):
        licence = entry["licence"]
        name = licence.get("spdx", "terms of use")
        lines = [f"### {entry['title']}", "",
                 f"- Entry: `{entry['id']}`",
                 f"- Attribution: {licence['attribution']}",
                 f"- Licence: [{name}]({licence['url']}), as stated at <{licence['evidence']}>",
                 f"- Verified redistribution permission: "
                 f"{'yes' if entry['redistributable'] else 'no'}",
                 f"- Use: {USES[entry['kind']]}"]
        lines += [f"- Source: <{url}>" for url in entry["urls"]]
        if "notes" in licence:
            lines.append(f"- Notes: {licence['notes']}")
        return lines + [""]

    third_party = [entry for entry in entries if manifest.requires_attribution(entry)]
    attribution = [e for e in third_party if str(e["licence"].get("spdx", "")).startswith("CC-BY")]
    others = [e for e in third_party if e not in attribution]
    lines = [
        "# Conformance asset attribution", "",
        "Generated from [`manifest.yaml`](manifest.yaml) by",
        "`python3 tools/conformance/assets.py attribution`; CI rejects a stale copy.", "",
        "Alumbra does not commit these works. The asset fetcher downloads them into a",
        "local cache, or the remote checks stream them from their publishers. Alumbra",
        "uses every file and stream exactly as published and makes no adaptations.",
        "See the [conformance README](README.md) for the rules every entry follows.", "",
        "## Creative Commons Attribution works", ""]
    for entry in attribution:
        lines += section(entry)
    lines += ["## Other third-party works", ""]
    for entry in others:
        lines += section(entry)
    return "\n".join(lines).rstrip("\n") + "\n"


def render_cmake(entries):
    def bracket(value):
        return f"[=[{value}]=]"
    lines = [f"set(ALUMBRA_CONFORMANCE_ASSET_IDS {bracket(';'.join(e['id'] for e in entries))})"]
    for entry in entries:
        lines.append(f"set(ALUMBRA_CONFORMANCE_ASSET_KIND_{entry['id']} {entry['kind']})")
        lines.append(f"set(ALUMBRA_CONFORMANCE_ASSET_TAGS_{entry['id']} "
                     f"{bracket(';'.join(entry['tags']))})")
    return "\n".join(lines) + "\n"


def ctest_selection(ctest, build_dir, config, tags, run=subprocess.run):
    command = [ctest, "--test-dir", str(build_dir), "--show-only=json-v1"]
    if config:
        command += ["-C", config]
    for tag in tags:
        command += ["-L", f"^{tag}$"]
    result = run(command, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise AssetError(f"{' '.join(command)} failed: {result.stderr.strip()}")
    try:
        tests = json.loads(result.stdout)["tests"]
        names = {test["name"]: next((p["value"] for p in test.get("properties", [])
                                     if p.get("name") == "LABELS"), [])
                 for test in tests}
    except (ValueError, KeyError, TypeError, AttributeError) as error:
        raise AssetError(f"unexpected ctest --show-only output: {error!r}") from error
    selected = set()
    for name, labels in names.items():
        if not name.startswith("conformance.asset."):
            continue
        if "conformance" not in labels:
            raise AssetError(f"{name} lacks the conformance label")
        selected.add(name)
    return selected


def _split(value):
    return [item for item in (value or "").split(",") if item]


def _selection(args, document):
    tags = _split(args.tags)
    unknown = [tag for tag in tags if tag not in document["tags"]]
    if unknown:
        raise manifest.ManifestError(f"unknown tags: {', '.join(unknown)}")
    kinds = _split(args.kind)
    bad = [kind for kind in kinds if kind not in manifest.KINDS]
    if bad:
        raise manifest.ManifestError(f"unknown kinds: {', '.join(bad)}")
    selected = manifest.select(document["entries"], tags, _split(args.ids), kinds)
    if not selected:
        raise manifest.ManifestError("no manifest entries match the selection")
    return selected


def _each(entries, action, log):
    status = 0
    for entry in entries:
        try:
            log(f"{entry['id']}: {action(entry)}")
        except Unavailable as error:
            log(f"{entry['id']}: unavailable: {error}")
            status = status or UNAVAILABLE
        except AssetError as error:
            log(f"{entry['id']}: FAILED: {error}")
            status = 1
    return status


def _positive(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return number


def parser():
    result = argparse.ArgumentParser(description="Validate, fetch and verify conformance assets.")
    result.add_argument("--manifest", default=str(manifest.DEFAULT_MANIFEST))
    result.add_argument("--cache", help="cache directory (default: $ALUMBRA_ASSET_CACHE)")
    result.add_argument("--offline", action="store_true",
                        help="never use the network (also: ALUMBRA_ASSET_OFFLINE=1)")
    result.add_argument("--attempts", type=_positive, default=3, help="attempts per URL")
    commands = result.add_subparsers(dest="command", required=True)
    commands.add_parser("validate", help="validate the manifest")
    for name in ("list", "fetch", "probe", "path"):
        command = commands.add_parser(name)
        command.add_argument("--tags", help="comma-separated tags an entry must all have")
        command.add_argument("--ids", help="comma-separated entry ids")
        command.add_argument("--kind", help="comma-separated kinds: " + ", ".join(manifest.KINDS))
        if name == "fetch":
            command.add_argument("--max-size", type=_positive,
                                 help="skip downloads larger than this many bytes")
            command.add_argument("--probe", action="store_true",
                                 help="also verify downloaded media with ffprobe")
    attribution = commands.add_parser("attribution", help="write ATTRIBUTION.md")
    attribution.add_argument("--check", action="store_true")
    attribution.add_argument("--output", default=str(ATTRIBUTION))
    cmake = commands.add_parser("cmake", help="write CTest registration data")
    cmake.add_argument("--output", required=True)
    labels = commands.add_parser("check-labels", help="compare tag selection with CTest labels")
    labels.add_argument("--ctest", required=True)
    labels.add_argument("--build-dir", required=True)
    labels.add_argument("--config", default="")
    labels.add_argument("--combination", action="append", default=[],
                        help="extra comma-separated tag combination to check")
    return result


def run(argv, environ=None, network=None, out=None, err=None):
    environ = dict(os.environ if environ is None else environ)
    out = out or (lambda message: print(message))
    err = err or (lambda message: print(message, file=sys.stderr))
    args = parser().parse_args(argv)
    try:
        document = manifest.load(args.manifest)
        entries = document["entries"]
        if args.command == "validate":
            out(f"{args.manifest}: {len(entries)} entries are valid")
            return 0
        if args.command == "attribution":
            text = render_attribution(entries)
            target = pathlib.Path(args.output)
            if args.check:
                if not target.is_file() or target.read_text(encoding="utf-8") != text:
                    out(f"{target} is stale; run tools/conformance/assets.py attribution")
                    return 1
                return 0
            target.write_text(text, encoding="utf-8")
            return 0
        if args.command == "cmake":
            target = pathlib.Path(args.output)
            text = render_cmake(entries)
            if not target.is_file() or target.read_text(encoding="utf-8") != text:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(text, encoding="utf-8")
            return 0
        if args.command == "check-labels":
            combinations = [[tag] for tag in document["tags"]]
            combinations += [_split(value) for value in args.combination]
            status = 0
            for tags in combinations:
                expected = {f"conformance.asset.{entry['id']}"
                            for entry in manifest.select(entries, tags)}
                actual = ctest_selection(args.ctest, args.build_dir, args.config, tags)
                if actual != expected or not expected:
                    out(f"--tags {','.join(tags)} selects {sorted(expected)}, "
                        f"but CTest labels select {sorted(actual)}")
                    status = 1
            if status == 0:
                out(f"{len(combinations)} tag selections match their CTest labels")
            return status
        selected = _selection(args, document)
        if args.command == "list":
            for entry in selected:
                out(entry["id"])
            return 0
        offline = args.offline or offline_from(environ)
        if network is None:
            network = Network(attempts=args.attempts, offline=offline)
        network.offline = offline
        cache = Cache(args.cache or default_cache(environ))
        fetcher = Fetcher(cache, network, environ, log=out)
        if args.command == "path":
            if len(selected) != 1:
                raise manifest.ManifestError("path needs a selection of exactly one entry")
            out(str(fetcher.path(selected[0])))
            return 0
        if args.command == "probe":
            return _each(selected, fetcher.probe, out)

        def fetch(entry):
            if (args.max_size is not None and entry["kind"] == "download"
                    and entry["size"] > args.max_size):
                return f"skipped: {entry['size']} bytes exceeds --max-size"
            path, state = fetcher.fetch(entry, probe=args.probe)
            return state if path is None else f"{state} {path}"
        return _each(selected, fetch, out)
    except manifest.ManifestError as error:
        err(str(error))
        return 2
    except AssetError as error:
        err(str(error))
        return 1


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
