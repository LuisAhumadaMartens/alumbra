import copy
import pathlib
import tempfile
import unittest

import manifest

DOCUMENT = manifest.load()
PLATFORMS, ERRORS = manifest.reference_vocabularies()


def entry(identifier):
    return next(item for item in DOCUMENT["entries"] if item["id"] == identifier)


class ManifestTest(unittest.TestCase):
    def setUp(self):
        self.document = copy.deepcopy(DOCUMENT)

    def check(self, message, mutate):
        document = copy.deepcopy(self.document)
        mutate(document)
        with self.assertRaisesRegex(manifest.ManifestError, message):
            manifest.validate(document, PLATFORMS, ERRORS)

    def edit(self, identifier, **changes):
        def mutate(document):
            target = next(item for item in document["entries"] if item["id"] == identifier)
            for field, value in changes.items():
                if value is None:
                    target.pop(field, None)
                else:
                    target[field] = value
        return mutate

    def edit_licence(self, identifier, **changes):
        def mutate(document):
            target = next(item for item in document["entries"] if item["id"] == identifier)
            for field, value in changes.items():
                if value is None:
                    target["licence"].pop(field, None)
                else:
                    target["licence"][field] = value
        return mutate

    def test_committed_manifest_is_valid_and_covers_every_kind(self):
        kinds = {item["kind"] for item in DOCUMENT["entries"]}
        self.assertEqual(kinds, set(manifest.KINDS))
        for item in DOCUMENT["entries"]:
            with self.subTest(entry=item["id"]):
                self.assertTrue(item["provenance"])
                self.assertIn("evidence", item["licence"])
                if item["kind"] == "download":
                    self.assertRegex(item["sha256"], manifest.SHA256)

    def test_every_entry_needs_licence_and_provenance(self):
        download = "tears-of-steel-stereo-opus"
        self.check(f"{download}: missing fields: licence", self.edit(download, licence=None))
        self.check(f"{download}: missing fields: provenance", self.edit(download, provenance=None))
        self.check(f"{download}: provenance must be nonempty",
                   self.edit(download, provenance=" "))
        self.check(f"{download}.licence: missing fields: evidence",
                   self.edit_licence(download, evidence=None))
        self.check(f"{download}: licence.url must be an https URL",
                   self.edit_licence(download, url="http://example.com/licence"))
        self.check(f"{download}: third-party and CC BY entries need licence.attribution",
                   self.edit_licence(download, attribution=None))
        generated = "frame-numbered-h264-mp4"
        self.check(f"{generated}: missing fields: licence", self.edit(generated, licence=None))

    def test_downloads_need_checksums_and_redistribution_rights(self):
        download = "sintel-trailer-audio-flac"
        self.check(f"{download}: missing fields: sha256", self.edit(download, sha256=None))
        self.check(f"{download}: missing fields: size", self.edit(download, size=None))
        self.check(f"{download}: sha256 must be", self.edit(download, sha256="ABC"))
        self.check(f"{download}: size must be", self.edit(download, size=0))
        self.check(f"{download}: urls must be", self.edit(download, urls=["http://example.com/a"]))
        self.check(f"{download}: urls must be", self.edit(download, urls=[]))
        self.check(f"{download}: downloaded copies need verified redistribution rights",
                   self.edit(download, redistributable=False))
        self.check(f"{download}: redistributable entries need an SPDX licence",
                   self.edit_licence(download, spdx=None))
        self.check(f"{download}: redistributable entries need an SPDX licence",
                   self.edit_licence(download, spdx="LicenseRef-Unknown"))
        remote = "apple-bipbop-advanced-hls"
        self.check(f"{remote}: unknown fields: sha256",
                   self.edit(remote, sha256=entry(download)["sha256"]))

    def test_tags_and_vocabularies(self):
        remote = "dashif-livesim2-testpic-2s-live"
        self.check(f"{remote}: tags not declared", self.edit(remote, tags=["dash", "remote", "x"]))
        self.check(f"{remote}: the 'remote' tag is required",
                   self.edit(remote, tags=["dash", "live"]))
        self.check(f"{remote}: tags must include the protocol",
                   self.edit(remote, tags=["remote", "live"]))
        self.check(f"{remote}: live entries cannot declare a fixed duration",
                   self.edit(remote, expected={"outcome": "plays",
                                               "duration": {"value": 1, "timescale": 1}}))
        self.check(f"{remote}: platforms must be drawn from",
                   self.edit(remote, platforms=["linux", "amiga"]))
        self.check("tags.unused: declared but not used",
                   lambda document: document["tags"].update(unused="Unused."))
        generated = "tone-1khz-2khz-aac-m4a"
        self.check(f"{generated}: the 'generated' tag is required",
                   self.edit(generated, tags=["progressive", "mp4", "aac"]))
        self.check(f"{generated}: generator test/conformance/generators/missing.sh does not exist",
                   self.edit(generated, generator="test/conformance/generators/missing.sh"))

    def test_expected_outcomes(self):
        target = "frame-numbered-h264-mp4"
        self.check(f"{target}: expected.error is required exactly when the outcome is error",
                   self.edit(target, expected={"outcome": "error"}))
        self.check(f"{target}: expected.error 'NoSuchError' is not in the error catalogue",
                   self.edit(target, expected={"outcome": "error", "error": "NoSuchError"}))
        self.check(f"{target}: expected.outcome must be one of",
                   self.edit(target, expected={"outcome": "maybe"}))
        self.check(f"{target}.expected.tracks\\[0\\]: codec 'avc1.42c01e' is not listed",
                   self.edit(target, expected={"outcome": "plays", "tracks": [
                       {"type": "video", "codec": "avc1.42c01e"}]}))
        self.check(f"{target}: AVC codecs must be avc1 or avc3 with six hexadecimal digits",
                   self.edit(target, codecs=["avc1.64"]))
        self.check(f"{target}: codecs the media check cannot verify: mp4a.6B",
                   self.edit(target, codecs=["mp4a.6B"]))
        document = copy.deepcopy(self.document)
        remote = next(i for i in document["entries"] if i["id"] == "apple-bipbop-advanced-hls")
        remote["codecs"].append("dvh1.05.06")
        manifest.validate(document, PLATFORMS, ERRORS)
        self.check(f"{target}: expected.duration value and timescale must be positive",
                   self.edit(target, expected={"outcome": "plays",
                                               "duration": {"value": 1, "timescale": 0}}))
        document = copy.deepcopy(self.document)
        item = next(i for i in document["entries"] if i["id"] == target)
        item["expected"] = {"outcome": "error", "error": "CodecUnsupported"}
        manifest.validate(document, PLATFORMS, ERRORS)

    def test_ids_and_schema(self):
        self.check("tears-of-steel-stereo-opus: duplicate id",
                   lambda document: document["entries"].append(
                       copy.deepcopy(entry("tears-of-steel-stereo-opus"))))
        self.check("entries\\[0\\]: id must be",
                   self.edit("tears-of-steel-720p-h264-mp3-mov", id="Upper_Case"))
        self.check("manifest: schema must be 1", lambda document: document.update(schema=2))
        self.check("manifest: schema must be 1", lambda document: document.update(schema=True))
        self.check("manifest: unknown fields: extra", lambda document: document.update(extra=1))

    def test_problems_are_reported_together(self):
        def mutate(document):
            self.edit("tears-of-steel-stereo-opus", provenance=None)(document)
            self.edit("sintel-trailer-audio-flac", sha256=None)(document)
        document = copy.deepcopy(self.document)
        mutate(document)
        with self.assertRaises(manifest.ManifestError) as raised:
            manifest.validate(document, PLATFORMS, ERRORS)
        self.assertIn("tears-of-steel-stereo-opus", str(raised.exception))
        self.assertIn("sintel-trailer-audio-flac", str(raised.exception))

    def test_load_reports_parse_errors_with_the_path(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "manifest.yaml"
            path.write_text("schema: yes\n", encoding="utf-8")
            with self.assertRaisesRegex(manifest.ManifestError, "manifest.yaml: line 1"):
                manifest.load(path)

    def test_selection_requires_every_tag(self):
        entries = DOCUMENT["entries"]
        selected = [item["id"] for item in manifest.select(entries, ["progressive", "h264"])]
        self.assertEqual(selected, [item["id"] for item in entries
                                    if {"progressive", "h264"} <= set(item["tags"])])
        self.assertIn("tears-of-steel-720p-h264-mp3-mov", selected)
        self.assertNotIn("tears-of-steel-stereo-opus", selected)
        self.assertEqual(
            [item["id"] for item in manifest.select(entries, ["remote"], kinds=["download"])], [])
        with self.assertRaisesRegex(manifest.ManifestError, "unknown entry ids: missing"):
            manifest.select(entries, ids=["missing"])


if __name__ == "__main__":
    unittest.main()
