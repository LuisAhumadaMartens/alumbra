import hashlib
import http.server
import json
import pathlib
import shutil
import subprocess
import tempfile
import threading
import unittest
import urllib.error
import urllib.parse
import urllib.request

import assets
import manifest

PAYLOAD = b"alumbra conformance payload\n" * 64
DIGEST = hashlib.sha256(PAYLOAD).hexdigest()
TEMPLATE = """schema: 1
tags:
  progressive: Progressive.
  hls: HLS.
  remote: Remote.
  aac: AAC.
entries:
  - id: clip
    title: Clip
    kind: download
    urls:
{urls}
    sha256: "{digest}"
    size: {size}
    file: clip.mp4
    provenance: Served by the local test fixture.
    licence:
      spdx: CC-BY-4.0
      url: https://creativecommons.org/licenses/by/4.0/
      evidence: https://assets.example/licence
      attribution: Test Author
    redistributable: true
    protocol: progressive
    container: mp4
    codecs: [mp4a.40.2]
    tags: [progressive, aac]
    platforms: [linux]
    expected:
      outcome: plays
  - id: stream
    title: Stream
    kind: remote
    urls:
      - https://assets.example/master.m3u8
    provenance: Served by the local test fixture.
    licence:
      url: https://assets.example/terms
      evidence: https://assets.example/terms
      attribution: Test Publisher
    redistributable: false
    protocol: hls
    container: mp4
    codecs: [mp4a.40.2]
    tags: [hls, aac, remote]
    platforms: [linux]
    expected:
      outcome: plays
"""


class Fixture:
    def __init__(self):
        self.routes = {}
        self.requests = []
        fixture = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def respond(self, body):
                fixture.requests.append((self.command, self.path))
                responses = (fixture.routes.get((self.command, self.path))
                             or fixture.routes.get(self.path, [(404, b"missing")]))
                status, payload = responses.pop(0) if len(responses) > 1 else responses[0]
                self.send_response(status)
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                if body:
                    self.wfile.write(payload)

            def do_GET(self):
                self.respond(True)

            def do_HEAD(self):
                self.respond(False)

            def log_message(self, *arguments):
                pass

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever,
                                       kwargs={"poll_interval": 0.01}, daemon=True)
        self.thread.start()

    def url(self, path):
        return f"http://127.0.0.1:{self.server.server_address[1]}{path}"

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()


class LoopbackOpener:
    def __init__(self, fixture):
        self.fixture = fixture
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def open(self, request, timeout):
        path = urllib.parse.urlsplit(request.full_url).path
        local = urllib.request.Request(self.fixture.url(path), method=request.get_method(),
                                       headers=dict(request.header_items()))
        return self.opener.open(local, timeout=timeout)


class ForbiddenOpener:
    def open(self, request, timeout):
        raise AssertionError(f"unexpected network access to {request.full_url}")


class FetcherTest(unittest.TestCase):
    def setUp(self):
        self.fixture = Fixture()
        self.addCleanup(self.fixture.close)
        self.directory = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.directory, ignore_errors=True)
        self.cache = self.directory / "cache"
        self.sleeps = []
        self.logs = []
        self.write_manifest(["https://assets.example/clip.mp4"])

    def write_manifest(self, urls, digest=DIGEST, size=len(PAYLOAD)):
        listed = "\n".join(f"      - {url}" for url in urls)
        self.manifest = self.directory / "manifest.yaml"
        self.manifest.write_text(TEMPLATE.format(urls=listed, digest=digest, size=size),
                                 encoding="utf-8")

    def network(self, opener=None):
        return assets.Network(opener=opener or LoopbackOpener(self.fixture),
                              sleep=self.sleeps.append, log=self.logs.append)

    def run_tool(self, *arguments, opener=None, environ=None):
        lines = []
        network = self.network(opener)
        status = assets.run(["--manifest", str(self.manifest), "--cache", str(self.cache),
                             *arguments], environ=environ or {}, network=network,
                            out=lines.append, err=lines.append)
        return status, "\n".join(lines), network

    def test_download_then_second_run_is_served_from_cache_without_network(self):
        self.fixture.routes["/clip.mp4"] = [(200, PAYLOAD)]
        status, output, _ = self.run_tool("fetch", "--ids", "clip")
        self.assertEqual(status, 0, output)
        self.assertIn("clip: downloaded", output)
        cached = self.cache / "sha256" / DIGEST / "clip.mp4"
        self.assertEqual(cached.read_bytes(), PAYLOAD)
        status, output, network = self.run_tool("fetch", "--ids", "clip", opener=ForbiddenOpener())
        self.assertEqual(status, 0, output)
        self.assertIn(f"clip: cached {cached}", output)
        self.assertEqual(network.requests, 0)
        status, output, network = self.run_tool("--offline", "fetch", "--tags", "progressive",
                                                opener=ForbiddenOpener())
        self.assertEqual((status, network.requests), (0, 0), output)
        status, output, _ = self.run_tool("path", "--ids", "clip", opener=ForbiddenOpener())
        self.assertEqual((status, output), (0, str(cached)))

    def test_checksum_mismatch_fails_with_the_entry_id(self):
        tampered = b"X" + PAYLOAD[1:]
        self.fixture.routes["/clip.mp4"] = [(200, tampered)]
        status, output, network = self.run_tool("fetch", "--ids", "clip")
        self.assertEqual(status, 1)
        self.assertRegex(output, r"^clip: FAILED: .*https://assets\.example/clip\.mp4: "
                                 r"SHA-256 mismatch: expected " + DIGEST)
        self.assertIn(hashlib.sha256(tampered).hexdigest(), output)
        self.assertEqual(network.requests, 1)
        self.assertEqual(self.sleeps, [])
        self.assertFalse((self.cache / "sha256").exists())
        self.assertEqual(list((self.cache / "tmp").iterdir()), [])

    def test_size_mismatch_and_oversized_responses_fail(self):
        for body in (PAYLOAD[:-1], PAYLOAD + b"!"):
            with self.subTest(size=len(body)):
                self.fixture.routes["/clip.mp4"] = [(200, body)]
                status, output, _ = self.run_tool("fetch", "--ids", "clip")
                self.assertEqual(status, 1)
                self.assertIn("clip: FAILED", output)
                self.assertIn("size mismatch", output)

    def test_retries_with_exponential_backoff(self):
        self.fixture.routes["/clip.mp4"] = [(503, b"busy"), (503, b"busy"), (200, PAYLOAD)]
        status, output, network = self.run_tool("fetch", "--ids", "clip")
        self.assertEqual(status, 0, output)
        self.assertEqual(self.sleeps, [1.0, 2.0])
        self.assertEqual(network.requests, 3)
        self.assertEqual(self.logs, [
            "https://assets.example/clip.mp4: HTTP 503 Service Unavailable; retrying in 1 s",
            "https://assets.example/clip.mp4: HTTP 503 Service Unavailable; retrying in 2 s"])

    def test_exhausted_retries_are_reported_as_unavailable(self):
        self.fixture.routes["/clip.mp4"] = [(503, b"busy")]
        status, output, network = self.run_tool("fetch", "--ids", "clip")
        self.assertEqual(status, assets.UNAVAILABLE)
        self.assertIn("clip: unavailable", output)
        self.assertIn("after 3 attempts", output)
        self.assertEqual((network.requests, self.sleeps), (3, [1.0, 2.0]))

    def test_falls_back_to_the_next_url_without_retrying_permanent_errors(self):
        self.write_manifest(["https://assets.example/gone.mp4", "https://assets.example/clip.mp4"])
        self.fixture.routes["/clip.mp4"] = [(200, PAYLOAD)]
        status, output, network = self.run_tool("fetch", "--ids", "clip")
        self.assertEqual(status, 0, output)
        self.assertEqual(network.requests, 2)
        self.assertEqual(self.sleeps, [])

    def test_offline_mode_fails_visibly_for_uncached_entries(self):
        for arguments, environ in ((("--offline",), {}), ((), {"ALUMBRA_ASSET_OFFLINE": "1"})):
            with self.subTest(environ=environ):
                status, output, network = self.run_tool(
                    *arguments, "fetch", "--ids", "clip", opener=ForbiddenOpener(),
                    environ=environ)
                self.assertEqual((status, network.requests), (1, 0))
                self.assertIn("clip: FAILED: not in the cache, and offline mode", output)

    def test_corrupt_cache_entry_is_replaced(self):
        cached = self.cache / "sha256" / DIGEST / "clip.mp4"
        cached.parent.mkdir(parents=True)
        cached.write_bytes(b"corrupt")
        self.fixture.routes["/clip.mp4"] = [(200, PAYLOAD)]
        status, output, _ = self.run_tool("fetch", "--ids", "clip")
        self.assertEqual(status, 0, output)
        self.assertIn("cached copy is corrupt", output)
        self.assertEqual(cached.read_bytes(), PAYLOAD)
        cached.write_bytes(b"corrupt")
        status, output, _ = self.run_tool("--offline", "fetch", "--ids", "clip")
        self.assertEqual(status, 1)
        self.assertFalse(cached.exists())

    def test_tag_selection(self):
        self.fixture.routes["/clip.mp4"] = [(200, PAYLOAD)]
        status, output, _ = self.run_tool("list", "--tags", "aac")
        self.assertEqual((status, output), (0, "clip\nstream"))
        status, output, _ = self.run_tool("list", "--tags", "aac,remote")
        self.assertEqual((status, output), (0, "stream"))
        status, output, _ = self.run_tool("list", "--tags", "progressive", "--kind", "remote")
        self.assertEqual((status, output), (2, "no manifest entries match the selection"))
        status, output, _ = self.run_tool("list", "--tags", "h265")
        self.assertEqual((status, output), (2, "unknown tags: h265"))
        status, output, network = self.run_tool("fetch", "--tags", "hls")
        self.assertEqual((status, network.requests), (0, 0))
        self.assertIn("stream: remote; streamed at test time", output)

    def test_repository_tag_selection(self):
        lines = []
        status = assets.run(["list", "--tags", "progressive,h264"], environ={},
                            out=lines.append)
        self.assertEqual(status, 0)
        self.assertEqual(lines, [entry["id"] for entry in manifest.select(
            manifest.load()["entries"], ["progressive", "h264"])])
        self.assertIn("tears-of-steel-720p-h264-mp3-mov", lines)

    def test_probe_checks_remote_signatures_and_download_lengths(self):
        self.fixture.routes["/master.m3u8"] = [(200, b"\xef\xbb\xbf#EXTM3U\n#EXT-X-VERSION:6\n")]
        self.fixture.routes["/clip.mp4"] = [(200, PAYLOAD)]
        status, output, _ = self.run_tool("probe")
        self.assertEqual(status, 0, output)
        self.assertIn(("HEAD", "/clip.mp4"), self.fixture.requests)
        self.fixture.routes["/master.m3u8"] = [(200, b"<html>moved</html>")]
        status, output, _ = self.run_tool("probe", "--ids", "stream")
        self.assertEqual(status, 1)
        self.assertIn("stream: FAILED: https://assets.example/master.m3u8: not an HLS playlist",
                      output)
        self.fixture.routes["/clip.mp4"] = [(200, PAYLOAD[:-1])]
        status, output, _ = self.run_tool("probe", "--ids", "clip")
        self.assertEqual(status, 1)
        self.assertIn("Content-Length", output)
        status, output, _ = self.run_tool("--offline", "probe", "--ids", "stream")
        self.assertEqual(status, assets.UNAVAILABLE)
        self.assertIn("stream: unavailable", output)

    def test_probe_falls_back_to_a_ranged_get_when_head_is_refused(self):
        self.fixture.routes[("HEAD", "/clip.mp4")] = [(405, b"")]
        self.fixture.routes[("GET", "/clip.mp4")] = [(200, PAYLOAD)]
        status, output, _ = self.run_tool("probe", "--ids", "clip")
        self.assertEqual(status, 0, output)
        self.assertEqual(self.fixture.requests, [("HEAD", "/clip.mp4"), ("GET", "/clip.mp4")])

    def test_local_write_errors_are_not_retried_as_outages(self):
        self.fixture.routes["/clip.mp4"] = [(200, PAYLOAD)]
        network = self.network()
        missing = self.directory / "missing" / "clip.mp4"
        with self.assertRaisesRegex(assets.AssetError, "cannot write") as raised:
            network.download("https://assets.example/clip.mp4", missing, len(PAYLOAD), DIGEST)
        self.assertNotIsInstance(raised.exception, assets.Unavailable)
        self.assertEqual((network.requests, self.sleeps), (1, []))

    def test_https_redirects_cannot_downgrade(self):
        handler = assets.SecureRedirects()
        request = urllib.request.Request("https://assets.example/a")
        with self.assertRaises(urllib.error.HTTPError) as raised:
            handler.redirect_request(request, None, 302, "Found", {}, "http://assets.example/a")
        raised.exception.close()
        self.assertIn("refusing a redirect from https to http", str(raised.exception))
        allowed = handler.redirect_request(request, None, 302, "Found", {},
                                           "https://mirror.example/a")
        self.assertEqual(allowed.full_url, "https://mirror.example/a")


class GeneratedTest(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        script = self.root / "test/conformance/generators/fake.sh"
        script.parent.mkdir(parents=True)
        script.write_text('printf generated > "$1"\n', encoding="utf-8")
        self.entry = {
            "id": "fake", "kind": "generated", "file": "fake.mp4", "container": "mp4",
            "generator": "test/conformance/generators/fake.sh",
            "expected": {"outcome": "plays", "duration": {"value": 300, "timescale": 30},
                         "tracks": [{"type": "video", "codec": "avc1.64001e", "width": 640,
                                     "height": 360, "frame_rate": 30, "frames": 300}]},
        }
        self.probe = {"format": {"format_name": "mov,mp4,m4a", "duration": "10.000000"},
                      "streams": [{"codec_type": "video", "codec_name": "h264",
                                   "profile": "High", "level": 30, "width": 640,
                                   "height": 360, "r_frame_rate": "30/1",
                                   "nb_read_packets": "300"}]}
        self.calls = []

    def run_command(self, command, **options):
        self.calls.append(command[0])
        if command[0] == "ffprobe":
            return subprocess.CompletedProcess(command, 0, json.dumps(self.probe), "")
        return subprocess.run(command, **options)

    def fetcher(self):
        return assets.Fetcher(assets.Cache(self.root / "cache"),
                              assets.Network(opener=ForbiddenOpener(), offline=True),
                              environ={"PATH": "/usr/bin:/bin"}, run=self.run_command,
                              root=self.root, log=lambda message: None)

    def test_generates_once_then_serves_the_cached_output(self):
        path, state = self.fetcher().fetch(self.entry)
        self.assertEqual((state, path.read_text()), ("generated", "generated"))
        self.assertEqual(self.calls, ["bash", "ffprobe"])
        path_again, state = self.fetcher().fetch(self.entry)
        self.assertEqual((path_again, state), (path, "cached"))
        self.assertEqual(self.calls, ["bash", "ffprobe", "ffprobe"])
        (self.root / self.entry["generator"]).write_text('printf other > "$1"\n')
        self.assertNotEqual(self.fetcher().fetch(self.entry)[0], path)

    def test_invalid_cached_clip_is_regenerated(self):
        path, _ = self.fetcher().fetch(self.entry)
        valid = dict(self.probe)
        self.probe = {"format": {}, "streams": []}
        original = self.run_command

        def recover(command, **options):
            if command[0] == "bash":
                self.probe = valid
            return original(command, **options)
        self.run_command = recover
        logs = []
        fetcher = self.fetcher()
        fetcher.log = logs.append
        path_again, state = fetcher.fetch(self.entry)
        self.assertEqual((path_again, state), (path, "generated"))
        self.assertIn("fake: cached clip is invalid", logs[0])

    def test_missing_ffprobe_keeps_the_cached_clip(self):
        path, _ = self.fetcher().fetch(self.entry)
        fetcher = self.fetcher()
        fetcher.environ["FFPROBE"] = str(self.root / "no-such-ffprobe")
        fetcher.run = subprocess.run
        with self.assertRaisesRegex(assets.ToolError, "is required to verify media"):
            fetcher.fetch(self.entry)
        self.assertTrue(path.is_file())

    def test_output_that_does_not_match_the_manifest_is_rejected(self):
        self.probe["streams"][0].update(level=31, nb_read_packets="299")
        with self.assertRaisesRegex(assets.AssetError, "level does not match.*frames 300 != 299"):
            self.fetcher().fetch(self.entry)
        self.assertFalse((self.root / "cache/generated").exists())

    def test_generator_failure_is_reported(self):
        (self.root / self.entry["generator"]).write_text("echo broken >&2\nexit 3\n")
        with self.assertRaisesRegex(assets.AssetError, "fake.sh failed: broken"):
            self.fetcher().fetch(self.entry)


class MediaTest(unittest.TestCase):
    def test_codec_families(self):
        self.assertEqual(manifest.codec_family("avc1.4d401f"), "h264")
        self.assertEqual(manifest.codec_family("hvc1.2.4.L123.B0"), "hevc")
        self.assertEqual(manifest.codec_family("mp4a.40.2"), "aac")
        self.assertEqual(manifest.codec_family("srt"), "subrip")
        self.assertIsNone(manifest.codec_family("unknown"))

    def test_media_problems(self):
        entry = {"container": "srt", "expected": {"outcome": "plays", "tracks": [
            {"type": "text", "codec": "srt", "cues": 76}]}}
        info = {"format": {"format_name": "srt"},
                "streams": [{"codec_type": "subtitle", "codec_name": "subrip",
                             "nb_read_packets": "76"}]}
        self.assertEqual(assets.media_problems(entry, info), [])
        info["streams"][0]["codec_type"] = "audio"
        self.assertEqual(assets.media_problems(entry, info),
                         ["track 0: expected a text track, found audio"])
        info["format"]["format_name"] = "ogg"
        info["streams"] = []
        self.assertEqual(assets.media_problems(entry, info),
                         ["container srt not in ogg", "expected 1 tracks, found 0"])


class OutputsTest(unittest.TestCase):
    def test_attribution_page_is_current_and_lists_cc_by_assets(self):
        document = manifest.load()
        text = assets.render_attribution(document["entries"])
        self.assertEqual(assets.ATTRIBUTION.read_text(encoding="utf-8"), text)
        for entry in document["entries"]:
            if entry["licence"].get("spdx", "").startswith("CC-BY"):
                self.assertIn(f"`{entry['id']}`", text)
                self.assertIn(entry["licence"]["attribution"], text)
            if entry["kind"] == "generated":
                self.assertNotIn(f"`{entry['id']}`", text)
        with tempfile.TemporaryDirectory() as directory:
            stale = pathlib.Path(directory) / "ATTRIBUTION.md"
            stale.write_text("stale\n", encoding="utf-8")
            lines = []
            self.assertEqual(assets.run(["attribution", "--check", "--output", str(stale)],
                                        out=lines.append), 1)
            self.assertIn("is stale", lines[0])
            self.assertEqual(assets.run(["attribution", "--output", str(stale)]), 0)
            self.assertEqual(stale.read_text(encoding="utf-8"), text)

    def test_cmake_data_carries_ids_kinds_and_tags(self):
        entries = manifest.load()["entries"]
        text = assets.render_cmake(entries)
        ids = ";".join(entry["id"] for entry in entries)
        self.assertIn(f"set(ALUMBRA_CONFORMANCE_ASSET_IDS [=[{ids}]=])", text)
        for entry in entries:
            self.assertIn(f"set(ALUMBRA_CONFORMANCE_ASSET_KIND_{entry['id']} {entry['kind']})",
                          text)
            self.assertIn(f"set(ALUMBRA_CONFORMANCE_ASSET_TAGS_{entry['id']} "
                          f"[=[{';'.join(entry['tags'])}]=])", text)

    def test_ctest_selection_uses_anchored_labels(self):
        commands = []

        def fake(command, **options):
            commands.append(command)
            tests = [
                {"name": "conformance.asset.a",
                 "properties": [{"name": "LABELS", "value": ["conformance", "h264"]}]},
                {"name": "other", "properties": [{"name": "LABELS", "value": ["h264"]}]},
            ]
            return subprocess.CompletedProcess(command, 0, json.dumps({"tests": tests}), "")
        selected = assets.ctest_selection("ctest", "build", "Debug", ["h264", "aes-128"], fake)
        self.assertEqual(selected, {"conformance.asset.a"})
        self.assertEqual(commands[0][-6:], ["-C", "Debug", "-L", "^h264$", "-L", "^aes-128$"])

        def broken(command, **options):
            return subprocess.CompletedProcess(command, 0, "warning: not json", "")
        with self.assertRaisesRegex(assets.AssetError, "unexpected ctest --show-only output"):
            assets.ctest_selection("ctest", "build", "", ["h264"], broken)


if __name__ == "__main__":
    unittest.main()
