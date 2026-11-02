import copy
import pathlib
import subprocess
import sys
import tempfile
import tomllib
import unittest
from unittest import mock

import validate


FIXTURES = pathlib.Path(__file__).parent / "fixtures"


class ValidatorTest(unittest.TestCase):
    def setUp(self):
        self.source = (FIXTURES / "valid/test.toml").read_text(encoding="utf-8")
        self.document = tomllib.loads(self.source)
        self.records = self.document["settings"]

    def validate_records(self, records=None, release="0.1.0"):
        document = dict(self.document, release=release,
                        settings=self.records if records is None else records)
        with mock.patch.object(validate.tomllib, "loads", return_value=document):
            return validate.validate_documents([("test.toml", "")])

    def test_valid_fixture_covers_all_types(self):
        result = validate.validate_documents(validate.read_directory(FIXTURES / "valid"))
        self.assertEqual({record["type"] for record in result[1].values()}, set(validate.META["types"]))
        self.assertEqual(len(result[1]), 8)
        self.assertEqual(len(result[2]), 1)

    def test_required_failure_fixtures(self):
        errors = {"duplicate": "duplicate key", "out_of_range": "outside range",
                  "missing_description": "description", "invalid_name": "invalid key name",
                  "unknown_type": "unknown type"}
        for fixture, message in errors.items():
            with self.subTest(fixture=fixture), self.assertRaisesRegex(validate.ValidationError, message):
                validate.validate_documents(validate.read_directory(FIXTURES / "invalid" / fixture))

    def test_bad_defaults_for_every_type(self):
        bad = {"boolean": 1, "integer": True, "double": 1,
               "enum": "missing", "string": False, "string_list": ["tag", 2],
               "duration": 1.5, "byte_size": -1}
        for record in self.records:
            with self.subTest(kind=record["type"]):
                record["default"] = bad[record["type"]]
                with self.assertRaises(validate.ValidationError):
                    self.validate_records([record])

    def test_malformed_metadata(self):
        cases = [
            ("description", "", "description"), ("description", [], "description"),
            ("since", "0.01.0", "invalid release"), ("since", "0.2.0", "after schema"),
            ("platforms", [], "empty"), ("platforms", ["planet"], "unknown list"),
            ("platforms", ["linux", "linux"], "duplicate"),
            ("audience", "developer", "unknown audience"), ("stability", "beta", "unknown stability"),
            ("apply", "runtime", "unknown apply"), ("unit", "seconds", "unknown unit"),
            ("mutability", "runtime", "unknown fields"),
            ("key", "test.web.enabled", "platform"), ("key", "test.disable_cmcd", "negative"),
            ("key", "test.disabled_cmcd", "negative"),
            ("key", "test.disabled_by_default", "negative"),
            ("key", "test.feature_disabled", "negative"),
            ("key", "test.feature_disable", "negative"),
            ("key", "test.feature_disabled_by_default", "negative"),
            ("key", "test.bad__name", "invalid"), ("key", "test.bad_", "invalid"),
            ("key", "test.name_ms", "unit"), ("key", "other.enabled", "namespace"),
            ("default", [], "boolean"),
        ]
        for field, value, message in cases:
            with self.subTest(field=field, value=value):
                record = dict(self.records[0], **{field: value})
                with self.assertRaisesRegex(validate.ValidationError, message):
                    self.validate_records([record])

    def test_numeric_bounds_and_units(self):
        cases = [
            (1, {"range": {"min": 5, "max": -5}}),
            (1, {"range": {"min": 0}}),
            (1, {"range": {"min": False, "max": 5}}),
            (1, {"range": {"min": 0, "max": 2**63}}),
            (1, {"range": {"min": 0.0, "max": 5.0}}),
            (2, {"default": float("nan")}),
            (2, {"range": {"min": 0.0, "max": float("inf")}}),
            (2, {"range": {"min": 0.0, "max": 10**400}}),
            (6, {"range": {"min": -1, "max": 1000}}),
            (6, {"unit": "", "key": "test.timeout"}),
            (7, {"unit": "ms", "key": "test.capacity_ms"}),
            (0, {"range": {"min": 0, "max": 1}}),
        ]
        for index, changes in cases:
            with self.subTest(index=index, changes=changes), self.assertRaises(validate.ValidationError):
                self.validate_records([dict(self.records[index], **changes)])
        for value in (-5, 5):
            self.validate_records([dict(self.records[1], default=value)])

    def test_allowed_values_documentation_and_category_defaults(self):
        for values in ([], [{"value": "auto"}],
                       [{"value": "auto", "description": ""}],
                       [{"value": "auto", "description": "Auto."}] * 2):
            with self.subTest(values=values), self.assertRaises(validate.ValidationError):
                self.validate_records([dict(self.records[3], values=values)])
        with self.assertRaisesRegex(validate.ValidationError, "documented values"):
            record = dict(self.records[3])
            del record["values"]
            self.validate_records([record])
        values = [{"value": "first", "description": "First tag."}]
        self.validate_records([dict(self.records[5], values=values)])
        with self.assertRaisesRegex(validate.ValidationError, "allowed values"):
            self.validate_records([dict(self.records[5], values=values, default=["unknown"])])
        with self.assertRaisesRegex(validate.ValidationError, "duplicate"):
            self.validate_records([dict(self.records[5], default=["first", "first"])])

    def test_alias_metadata_and_boundaries(self):
        cases = [
            ({"key": "test.old_ms"}, "missing"),
            ({"key": "test.old_ms", "since": "0.2.0"}, "alias since"),
            ({"key": "test.old_ms", "since": "0.0.0"}, "alias since"),
            ({"key": "test.Bad", "since": "0.1.0"}, "invalid key"),
            ({"key": "test.old_ms", "since": "0.1.0", "remove_in": "0.2.0"}, "entire next minor"),
            ({"key": "test.old_ms", "since": "0.1.0", "remove_in": "0.3.1"}, "patch zero"),
            ({"key": "test.old", "since": "0.1.0", "replacement": "test.enabled"}, "unknown fields"),
            ({"key": "test.old", "since": "0.1.0"}, "alias unit"),
        ]
        for alias, message in cases:
            with self.subTest(alias=alias), self.assertRaisesRegex(validate.ValidationError, message):
                self.validate_records([dict(self.records[6], aliases=[alias])])
        self.validate_records([dict(self.records[6], aliases=[{"key": "test.old_ms", "since": "0.1.0"}])])
        self.assertEqual(validate.removal_release("0.3.0", "0.1.9"), (0, 3, 0))
        self.assertEqual(validate.removal_release("2.0.0", "1.1.0"), (2, 0, 0))
        with self.assertRaisesRegex(validate.ValidationError, "major"):
            validate.removal_release("1.3.0", "1.1.0")
        with self.assertRaisesRegex(validate.ValidationError, "major"):
            validate.removal_release("1.5.0", "0.1.0")
        with self.assertRaisesRegex(validate.ValidationError, "expired"):
            self.validate_records(release="0.3.0")

    def test_global_uniqueness_and_deprecation(self):
        alias = {"key": "test.enabled", "since": "0.1.0"}
        with self.assertRaisesRegex(validate.ValidationError, "collides"):
            self.validate_records([self.records[0], dict(self.records[1], aliases=[alias])])
        with self.assertRaisesRegex(validate.ValidationError, "duplicate alias"):
            self.validate_records([dict(self.records[1], aliases=[alias, alias])])
        with self.assertRaisesRegex(validate.ValidationError, "duplicate key"):
            validate.validate_documents([("test.toml", self.source), ("test.toml", self.source)])
        old = dict(self.records[0], key="test.old_enabled",
                   deprecation={"since": "0.1.0", "replacement": "test.enabled"})
        self.validate_records([self.records[0], old])
        for deprecation in ({}, {"since": "0.2.0"}, {"since": "0.0.0"},
                            {"since": "0.1.0", "replacement": "test.missing"},
                            {"since": "0.1.0", "replacement": "test.old_enabled"},
                            {"since": "0.1.0", "replacement": "test.count"}):
            with self.subTest(deprecation=deprecation), self.assertRaises(validate.ValidationError):
                self.validate_records([self.records[0], self.records[1],
                                       dict(old, deprecation=deprecation)])
        with self.assertRaisesRegex(validate.ValidationError, "itself be deprecated"):
            self.validate_records([dict(self.records[0], deprecation={"since": "0.1.0"}), old])

    def test_auto_sentinel_is_explicit_and_validated(self):
        record = dict(self.records[1], default=0, range={"min": 0, "max": 5},
                      auto={"value": 0, "policy": "hardware_concurrency_budget"})
        self.validate_records([record])
        self.validate_records([dict(self.records[6], default=0, range={"min": 0, "max": 8},
                                   auto={"value": 0, "policy": "half_frame_interval_capped"})])
        for auto in ({"value": 1, "policy": "hardware_concurrency_budget"},
                     {"value": False, "policy": "hardware_concurrency_budget"},
                     {"value": 0, "policy": "unknown"}, {"value": 0}):
            with self.subTest(auto=auto), self.assertRaises(validate.ValidationError):
                self.validate_records([dict(record, auto=auto)])
        with self.assertRaises(validate.ValidationError):
            self.validate_records([dict(record, default=1)])
        with self.assertRaises(validate.ValidationError):
            self.validate_records([dict(record, range={"min": -1, "max": 5})])

    def test_root_validation(self):
        for document in ({}, dict(self.document, schema_version=True),
                         dict(self.document, settings=[]),
                         dict(self.document, extra="unknown"),
                         dict(self.document, settings=[None])):
            with self.subTest(document=document), mock.patch.object(
                    validate.tomllib, "loads", return_value=document):
                with self.assertRaises(validate.ValidationError):
                    validate.validate_documents([("test.toml", "")])
        with self.assertRaisesRegex(validate.ValidationError, "no schema"):
            validate.validate_documents([])
        with self.assertRaises(validate.ValidationError):
            validate.validate_documents([("test.toml", "not valid TOML")])
        sources = [("test.toml", self.source),
                   ("other.toml", self.source.replace('release = "0.1.0"', 'release = "0.2.0"')
                    .replace('key = "test.', 'key = "other.')
                    .replace('replacement = "test.', 'replacement = "other.'))]
        with self.assertRaisesRegex(validate.ValidationError, "same release"):
            validate.validate_documents(sources)

    def test_history_preserves_deprecation_and_release(self):
        old = self.validate_records()
        records = copy.deepcopy(self.records)
        records[7]["deprecation"] = {"since": "0.2.0"}
        with self.assertRaisesRegex(validate.ValidationError, "deprecation start"):
            validate.validate_history(self.validate_records(records, "0.2.0"), old)
        del records[7]["deprecation"]
        with self.assertRaisesRegex(validate.ValidationError, "deprecation start"):
            validate.validate_history(self.validate_records(records, "0.2.0"), old)
        with self.assertRaisesRegex(validate.ValidationError, "backwards"):
            validate.validate_history(old, ((0, 2, 0), old[1], old[2]))

    def test_git_history_reads_schema_from_explicit_ref(self):
        results = [
            subprocess.CompletedProcess([], 0, "settings/schema/test.toml\nsettings/schema/README.md\n", ""),
            subprocess.CompletedProcess([], 0, self.source, ""),
        ]
        with mock.patch.object(validate.subprocess, "run", side_effect=results) as run:
            documents = validate.read_ref("published-ref")
            self.assertEqual(len(validate.validate_documents(documents)[1]), 8)
            self.assertEqual(run.call_args_list[0].args[0],
                             ["git", "ls-tree", "-r", "--name-only", "published-ref", "--", "settings/schema"])
            self.assertEqual(run.call_args_list[1].args[0],
                             ["git", "show", "published-ref:settings/schema/test.toml"])

    def test_history_rejects_early_or_silent_alias_removal(self):
        old = self.validate_records()
        current_records = copy.deepcopy(self.records)
        del current_records[6]["aliases"]
        for release in ("0.1.1", "0.2.0", "0.2.99"):
            with self.subTest(release=release), self.assertRaisesRegex(
                    validate.ValidationError, "before compatibility window"):
                validate.validate_history(self.validate_records(current_records, release), old)
        validate.validate_history(self.validate_records(current_records, "0.3.0"), old)
        current_records[6]["aliases"] = [{"key": "test.old_timeout_ms", "since": "0.1.0"}]
        with self.assertRaisesRegex(validate.ValidationError, "shorten"):
            validate.validate_history(self.validate_records(current_records, "0.2.0"), old)

    def test_history_immutable_versions_targets_and_promised_windows(self):
        old = self.validate_records()
        cases = [
            (0, {"since": "0.2.0"}),
            (6, {"aliases": [{"key": "test.old_timeout_ms", "since": "0.2.0", "remove_in": "0.4.0"}]}),
            (6, {"aliases": [{"key": "test.old_timeout_ms", "since": "0.1.0", "remove_in": "0.3.0"}]}),
        ]
        old_later = copy.deepcopy(old)
        old_later[2]["test.old_timeout_ms"][1]["remove_in"] = "0.4.0"
        for index, change in cases:
            records = copy.deepcopy(self.records)
            records[index].update(change)
            previous = old_later if "remove_in" in change.get("aliases", [{}])[0] and index == 6 else old
            with self.subTest(change=change), self.assertRaises(validate.ValidationError):
                validate.validate_history(self.validate_records(records, "0.2.0"), previous)
        records = copy.deepcopy(self.records)
        records[6]["aliases"][0]["remove_in"] = "0.4.0"
        validate.validate_history(self.validate_records(records, "0.2.0"), old)
        records[6]["aliases"] = []
        records[6]["key"] = "test.other_timeout_ms"
        records[6]["aliases"] = [{"key": "test.old_timeout_ms", "since": "0.1.0", "remove_in": "0.3.0"}]
        with self.assertRaisesRegex(validate.ValidationError, "target"):
            validate.validate_history(self.validate_records(records, "0.2.0"), old)

    def test_history_major_only_removals(self):
        records = copy.deepcopy(self.records)
        records[6]["aliases"][0] = {"key": "test.old_timeout_ms", "since": "1.0.0", "remove_in": "2.0.0"}
        old = self.validate_records(records, "1.0.0")
        records[6]["aliases"] = []
        with self.assertRaises(validate.ValidationError):
            validate.validate_history(self.validate_records(records, "1.9.0"), old)
        validate.validate_history(self.validate_records(records, "2.0.0"), old)
        with self.assertRaisesRegex(validate.ValidationError, "major release"):
            validate.validate_history(self.validate_records(records[1:], "1.9.0"),
                                      self.validate_records(records, "1.0.0"))

    def test_new_alias_introduction_cannot_be_backdated(self):
        old_record = dict(self.records[0], key="test.old")
        new_record = dict(self.records[0], key="test.new",
                          aliases=[{"key": "test.old", "since": "0.4.0", "remove_in": "0.6.0"}])
        previous = self.validate_records([old_record], "0.4.0")
        with self.assertRaisesRegex(validate.ValidationError, "new alias since"):
            validate.validate_history(self.validate_records([new_record], "0.5.0"), previous)
        new_record["aliases"][0] = {"key": "test.old", "since": "0.5.0", "remove_in": "0.7.0"}
        current = self.validate_records([new_record], "0.5.0")
        validate.validate_history(current, previous)
        validate.validate_history(current, self.validate_records([old_record], "0.5.0"))
        validate.validate_history(current, self.validate_records(
            [dict(new_record, aliases=[])], "0.5.0"))
        for since in ("0.1.0", "0.4.0"):
            new_record["aliases"][0] = {"key": "test.old", "since": since}
            with self.subTest(since=since), self.assertRaisesRegex(
                    validate.ValidationError, "new alias since"):
                validate.validate_history(self.validate_records([new_record], "0.5.0"),
                                          self.validate_records([old_record], "0.5.0"))

    def test_actual_removals_crossing_one_require_major_boundaries(self):
        old_record = dict(self.records[0], key="test.kept",
                          aliases=[{"key": "test.old", "since": "0.1.0"}])
        new_record = dict(old_record, aliases=[])
        previous = self.validate_records([old_record], "0.5.0")
        for release in ("1.5.0", "1.0.1", "0.5.1"):
            with self.subTest(release=release), self.assertRaises(validate.ValidationError):
                validate.validate_history(self.validate_records([new_record], release), previous)
        validate.validate_history(self.validate_records([new_record], "1.0.0"), previous)
        previous = self.validate_records([old_record], "1.0.0")
        for release in ("1.5.0", "2.1.0", "2.0.1"):
            with self.subTest(release=release), self.assertRaisesRegex(
                    validate.ValidationError, "major release|patch zero"):
                validate.validate_history(self.validate_records([new_record], release), previous)
        validate.validate_history(self.validate_records([new_record], "2.0.0"), previous)

    def test_production_contract(self):
        _, records, _ = validate.validate_documents(validate.read_directory(validate.ROOT / "settings/schema"))
        self.assertGreaterEqual(len(records), 33)
        for key, default in (("playback.sync.mode", "audio"),
                             ("playback.display.rate_match", "seamless"),
                             ("demux.ffmpeg.mode", "fallback"),
                             ("decode.prefer_hardware", "auto"),
                             ("ui.controls.seek_step_ms", 5000),
                             ("decode.dropped_frames.max_percent", 10)):
            self.assertEqual(records[key]["default"], default)
        self.assertEqual(records["observability.log.level"]["default"], "info")
        self.assertEqual([entry["value"] for entry in records["observability.log.level"]["values"]],
                         ["verbose", "debug", "info", "warning", "error"])
        categories = {"net", "manifest", "abr", "buffer", "demux", "decode", "render",
                      "audio", "text", "ads", "drm", "settings", "player", "port"}
        for key in ("observability.log.categories", "observability.trace.categories"):
            self.assertEqual(records[key]["default"], [])
            self.assertEqual({entry["value"] for entry in records[key]["values"]}, categories)
            self.assertEqual(records[key]["apply"], "live")
        self.assertNotIn("playback.stall.timeout_ms", records)
        self.assertNotIn("playback.gap_jump.max_gap_ms", records)

    def test_cli_reports_success_failure_and_history_errors(self):
        script = pathlib.Path(validate.__file__)
        for directory, code in ((FIXTURES / "valid", 0),
                                (FIXTURES / "invalid/duplicate", 1)):
            result = subprocess.run([sys.executable, str(script), str(directory)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, code, result.stderr)
            self.assertIn("Validated" if code == 0 else "duplicate key",
                          result.stdout if code == 0 else result.stderr)
        result = subprocess.run([sys.executable, str(script), "--previous-ref", "not-a-ref"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("cannot read previous ref", result.stderr)
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([sys.executable, str(script), "--previous", directory],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("no TOML files", result.stderr)


if __name__ == "__main__":
    unittest.main()
