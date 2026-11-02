import contextlib
import hashlib
import io
import json
import pathlib
import subprocess
import tempfile
import unittest

import third_party


BASELINE = "a" * 40


class ThirdPartyTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.vcpkg = self.root / "vcpkg"
        self.installed = self.root / "installed"
        self.output = self.root / "THIRD_PARTY.md"
        self.policy = self.root / "policy.json"
        self.manifest = self.root / "vcpkg.json"
        self.repository_policy = third_party.read_json(
            pathlib.Path(__file__).with_name("licence_policy.json"))
        self.make_package("fixture", "1.0", "MIT", "https://example.test/fixture")
        self.make_vcpkg_checkout()
        self.write_manifest()
        self.write_policy({"allowed": ["MIT"], "conditional": {}, "reviewed_exceptions": [],
                           "or_choices": []})
        self.write_output()

    def make_package(self, name, version, expression, homepage, dependencies=None):
        port = self.vcpkg / "ports" / name
        port.mkdir(parents=True, exist_ok=True)
        port_manifest = {
            "name": name,
            "version": version,
            "homepage": homepage,
            "license": expression,
            "description": f"Fixture {name} dependency.",
            "dependencies": dependencies or [],
        }
        (port / "vcpkg.json").write_text(json.dumps(port_manifest), encoding="utf-8")
        triplet_root = self.installed / "x64-linux"
        share = triplet_root / "share" / name
        share.mkdir(parents=True, exist_ok=True)
        (share / "copyright").write_text("Fixture copyright and licence.", encoding="utf-8")
        (share / "vcpkg.spdx.json").write_text(json.dumps({
            "spdxVersion": "SPDX-2.3",
            "packages": [{
                "SPDXID": "SPDXRef-port",
                "name": name,
                "versionInfo": version,
                "licenseConcluded": expression,
                "licenseDeclared": "NOASSERTION",
            }],
        }), encoding="utf-8")
        info = self.installed / "vcpkg" / "info"
        info.mkdir(parents=True, exist_ok=True)
        (info / f"{name}_{version}_x64-linux.list").write_text(
            f"x64-linux/lib/lib{name}.a\n", encoding="utf-8")
        status = self.installed / "vcpkg" / "status"
        with status.open("a", encoding="utf-8") as stream:
            stream.write(
                f"Package: {name}\nVersion: {version}\nArchitecture: x64-linux\n"
                "Status: install ok installed\n\n")

    def make_vcpkg_checkout(self):
        self.vcpkg.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "init", "-q", str(self.vcpkg)], check=True)
        subprocess.run(["git", "-C", str(self.vcpkg), "config", "user.name", "CI tests"],
                       check=True)
        subprocess.run(["git", "-C", str(self.vcpkg), "config", "user.email",
                        "ci-tests@example.invalid"], check=True)
        subprocess.run(["git", "-C", str(self.vcpkg), "add", "ports"], check=True)
        subprocess.run(["git", "-C", str(self.vcpkg), "commit", "-qm", "fixture"], check=True)

    def write_manifest(self, dependencies=None):
        baseline = subprocess.run(
            ["git", "-C", str(self.vcpkg), "rev-parse", "HEAD"],
            check=True, capture_output=True, text=True).stdout.strip()
        (self.manifest).write_text(json.dumps({
            "builtin-baseline": baseline,
            "dependencies": dependencies or ["fixture"],
            "features": {"desktop": {"dependencies": ["fixture"]}, "tests": {"dependencies": []}},
        }), encoding="utf-8")

    def write_policy(self, policy):
        self.policy.write_text(json.dumps(policy), encoding="utf-8")

    def write_output(self, sections=None):
        sections = sections or {
            "arm64-osx": "### `arm64-osx`\n\nExisting platform.",
            "x64-linux": "### `x64-linux`\n\nStale output.",
        }
        body = "\n\n".join(sections[key] for key in sorted(sections))
        self.output.write_text(
            f"# Third-party dependencies\n\n{third_party.BEGIN}\n\n{body}\n"
            f"{third_party.END}\n\n## CI-only tools\n",
            encoding="utf-8")

    def arguments(self, command, features=("desktop", "tests")):
        arguments = [
            command,
            "--triplet", "x64-linux",
            "--installed-root", str(self.installed),
            "--vcpkg-root", str(self.vcpkg),
            "--manifest", str(self.manifest),
            "--policy", str(self.policy),
            "--output", str(self.output),
        ]
        for feature in features:
            arguments.extend(("--feature", feature))
        return arguments

    def fixture_record(self, expression):
        return {
            "port": "fixture",
            "version": "1.0",
            "expression": expression,
            "expression_present": True,
            "linkage": "static",
            "features": ["desktop"],
            "roles": ["target"],
            "port_features": [],
            "triplet": "x64-linux",
            "baseline": BASELINE,
            "upstream": "https://example.test/fixture",
            "copyright": "Fixture copyright and licence.",
        }

    def reviewed_policy(self, record, resolved_expression):
        return {
            **self.repository_policy,
            "reviewed_exceptions": [{
                "port": record["port"],
                "version": record["version"],
                "expression": record["expression"],
                "expression_present": record["expression_present"],
                "baseline": record["baseline"],
                "port_features": record["port_features"],
                "triplets": [record["triplet"]],
                "owner_features": record["features"],
                "roles": record["roles"],
                "linkage": record["linkage"],
                "resolved_expression": resolved_expression,
                "upstream": record["upstream"],
                "review": "Reviewed fixture exception.",
                "evidence": ["Fixture source evidence."],
            }],
        }

    def test_denied_gpl_names_port_and_policy_rule(self):
        for expression, rule in (
                ("GPL-3.0-only", "deny-gpl-and-agpl"),
                ("AGPL-3.0-only", "deny-gpl-and-agpl"),
                ("LGPL-3.0-only", "deny-lgpl-3"),
                ("LicenseRef-NonFree", "deny-nonfree")):
            with self.subTest(expression=expression):
                errors = []
                accepted = third_party.evaluate_expression(
                    third_party.parse_expression(expression),
                    self.fixture_record(expression), self.repository_policy, errors, False)
                self.assertFalse(accepted)
                self.assertIn("fixture:x64-linux", errors[0])
                self.assertIn(expression, errors[0])
                self.assertIn(rule, errors[0])

    def test_and_requires_all_terms_to_be_allowed(self):
        errors = []
        accepted = third_party.evaluate_expression(
            third_party.parse_expression("MIT AND Custom-1.0"),
            self.fixture_record("MIT AND Custom-1.0"),
            {"allowed": ["MIT"], "conditional": {}}, errors, False)
        self.assertFalse(accepted)
        self.assertIn("Custom-1.0", errors[0])

    def test_or_requires_a_recorded_eligible_branch(self):
        record = self.fixture_record("MIT OR GPL-3.0-only")
        policy = {"allowed": ["MIT"], "conditional": {}, "or_choices": [{
            "port": "fixture",
            "version": "1.0",
            "expression": "MIT OR GPL-3.0-only",
            "expression_present": True,
            "baseline": BASELINE,
            "port_features": [],
            "triplets": ["x64-linux"],
            "owner_features": ["desktop"],
            "roles": ["target"],
            "linkage": "static",
            "selected": "MIT",
            "review": "Reviewed fixture choice.",
            "evidence": ["Fixture approval."],
        }]}
        errors = []
        accepted = third_party.evaluate_expression(
            third_party.parse_expression(record["expression"]), record, policy, errors, False)
        self.assertTrue(accepted)
        self.assertEqual(errors, [])

    def test_with_exception_requires_a_separate_review(self):
        expression = "MIT WITH LLVM-exception"
        errors = []
        accepted = third_party.evaluate_expression(
            third_party.parse_expression(expression), self.fixture_record(expression),
            {"allowed": ["MIT"], "conditional": {}}, errors, False)
        self.assertFalse(accepted)
        self.assertIn("review-spdx-exceptions", errors[0])

    def test_with_exception_passes_only_with_exact_reviewed_terms(self):
        record = self.fixture_record("MIT WITH LLVM-exception")
        policy = {
            **self.repository_policy,
            "reviewed_exceptions": [{
                "port": "fixture",
                "version": "1.0",
                "expression": record["expression"],
                "expression_present": True,
                "baseline": BASELINE,
                "port_features": [],
                "triplets": ["x64-linux"],
                "owner_features": ["desktop"],
                "roles": ["target"],
                "linkage": "static",
                "resolved_expression": "MIT",
                "upstream": record["upstream"],
                "review": "Reviewed fixture exception.",
                "evidence": ["Fixture source evidence."],
            }],
        }
        errors = third_party.policy_failures([record], policy, self.vcpkg)
        self.assertEqual(errors, [])

    def test_conditional_lgpl_requires_dynamic_linkage(self):
        record = self.fixture_record("LGPL-2.1-only")
        record["linkage"] = "static"
        policy = {"allowed": [], "conditional": {
            "LGPL-2.1-only": {
                "features": ["desktop"],
                "linkage": "dynamic",
                "rule": "lgpl-dynamic",
                "triplets": ["x64-linux"],
            },
        }}
        errors = []
        accepted = third_party.evaluate_expression(
            third_party.parse_expression(record["expression"]), record, policy, errors, True)
        self.assertFalse(accepted)
        self.assertIn("requires dynamic linkage", errors[0])

    def test_missing_effective_spdx_expression_fails_policy(self):
        record = self.fixture_record(None)
        record["expression_present"] = False
        errors = third_party.policy_failures(
            [record], {"allowed": ["MIT"], "conditional": {}}, self.vcpkg)
        self.assertEqual(len(errors), 1)
        self.assertIn("licenseConcluded is missing", errors[0])
        self.assertIn("require-effective-licenseConcluded", errors[0])

    def test_explicit_null_effective_spdx_expression_fails_policy(self):
        record = self.fixture_record(None)
        record["expression_present"] = True
        errors = third_party.policy_failures(
            [record], {"allowed": ["MIT"], "conditional": {}}, self.vcpkg)
        self.assertEqual(len(errors), 1)
        self.assertIn("licenseConcluded is null", errors[0])
        self.assertIn("require-effective-licenseConcluded", errors[0])

    def test_missing_expression_can_pass_only_with_an_exact_review(self):
        record = self.fixture_record(None)
        record["expression_present"] = False
        policy = {
            **self.repository_policy,
            "reviewed_exceptions": [{
                "port": "fixture",
                "version": "1.0",
                "expression": None,
                "expression_present": False,
                "baseline": BASELINE,
                "port_features": [],
                "triplets": ["x64-linux"],
                "owner_features": ["desktop"],
                "roles": ["target"],
                "linkage": "static",
                "expected_manifest_license": "MIT",
                "resolved_expression": "MIT",
                "upstream": "https://example.test/fixture",
                "review": "Exact test review.",
                "evidence": ["Fixture source and copyright evidence."],
            }],
        }
        errors = third_party.policy_failures([record], policy, self.vcpkg)
        self.assertEqual(errors, [])

    def test_reviewed_exception_cannot_waive_a_denied_licence(self):
        record = self.fixture_record("GPL-3.0-only")
        policy = {
            "allowed": ["MIT"],
            "conditional": {},
            "denied": [{
                "pattern": "^(AGPL|GPL)-",
                "rule": "deny-gpl-and-agpl",
            }],
            "reviewed_exceptions": [{
                "port": "fixture",
                "version": "1.0",
                "expression": "GPL-3.0-only",
                "expression_present": True,
                "baseline": BASELINE,
                "port_features": [],
                "triplets": ["x64-linux"],
                "owner_features": ["desktop"],
                "roles": ["target"],
                "linkage": "static",
                "resolved_expression": "MIT",
                "review": "Invalid test waiver.",
                "evidence": ["Test evidence."],
            }],
        }
        errors = third_party.policy_failures([record], policy, self.vcpkg)
        self.assertIn("GPL-3.0-only is denied", errors[0])

    def test_review_cannot_replace_or_nested_in_and_without_branch_choice(self):
        record = self.fixture_record("MIT AND (BSD-3-Clause OR GPL-3.0-only)")
        policy = self.reviewed_policy(record, "MIT")
        errors = third_party.policy_failures([record], policy, self.vcpkg)
        self.assertTrue(any("review-spdx-or-choice" in error for error in errors))

    def test_review_cannot_erase_a_recorded_nested_or_choice(self):
        record = self.fixture_record("MIT AND (BSD-3-Clause OR GPL-3.0-only)")
        policy = self.reviewed_policy(record, "MIT")
        policy["or_choices"] = [{
            "port": record["port"],
            "version": record["version"],
            "expression": "BSD-3-Clause OR GPL-3.0-only",
            "expression_present": record["expression_present"],
            "baseline": record["baseline"],
            "port_features": record["port_features"],
            "triplets": [record["triplet"]],
            "owner_features": record["features"],
            "roles": record["roles"],
            "linkage": record["linkage"],
            "selected": "BSD-3-Clause",
            "review": "Reviewed nested choice.",
            "evidence": ["Fixture source evidence."],
        }]
        errors = third_party.policy_failures([record], policy, self.vcpkg)
        self.assertTrue(any(
            "BSD-3-Clause" in error
            and "preserve-original-license-obligations" in error
            for error in errors))

    def test_review_cannot_drop_known_and_obligation(self):
        record = self.fixture_record("MIT AND BSD-3-Clause")
        policy = self.reviewed_policy(record, "MIT")
        errors = third_party.policy_failures([record], policy, self.vcpkg)
        self.assertTrue(any(
            "BSD-3-Clause" in error
            and "preserve-original-license-obligations" in error
            for error in errors))

    def pkgconf_entry(self):
        return next(entry for entry in self.repository_policy["reviewed_exceptions"]
                    if entry["port"] == "pkgconf")

    def pkgconf_record(self, **overrides):
        entry = self.pkgconf_entry()
        text = entry["licence_texts"]["LicenseRef-pkgconf-COPYING"]["text"]
        port = self.vcpkg / "ports" / "pkgconf"
        port.mkdir(parents=True, exist_ok=True)
        (port / "vcpkg.json").write_text(
            json.dumps({"name": "pkgconf", "version": "3.0.3", "port-version": 1,
                        "license": None}),
            encoding="utf-8")
        record = {
            "port": "pkgconf",
            "version": "3.0.3#1",
            "expression": "LicenseRef-vcpkg-null",
            "expression_present": True,
            "linkage": "static",
            "features": ["desktop"],
            "roles": ["build"],
            "port_features": [],
            "triplet": "x64-linux",
            "baseline": entry["baseline"],
            "upstream": "https://github.com/pkgconf/pkgconf",
            "purpose": "Fixture pkgconf host tool.",
            "copyright": f"COPYING:\n\n{text}\n\ngetopt_long.c:\n\nFixture notices.\n",
            "copyright_sha256": entry["copyright_sha256"],
        }
        record.update(overrides)
        return record

    def pkgconf_policy(self, **overrides):
        entry = {**self.pkgconf_entry(), **overrides}
        return {**self.repository_policy, "reviewed_exceptions": [entry]}

    def test_reviewed_pkgconf_host_tool_exception_passes_on_each_triplet(self):
        for triplet in ("arm64-osx", "x64-linux"):
            with self.subTest(triplet=triplet):
                record = self.pkgconf_record(triplet=triplet)
                self.assertEqual(third_party.policy_failures(
                    [record], self.repository_policy, self.vcpkg), [])
                section = third_party.render_triplet(
                    [record], record["baseline"], triplet, self.repository_policy,
                    ["desktop", "tests"])
                self.assertIn("reviewed as LicenseRef-pkgconf-COPYING", section)
                self.assertIn("build-time host tool, not distributed", section)

    def test_pkgconf_exception_does_not_cover_other_versions_or_baselines(self):
        for overrides in ({"version": "3.0.3"}, {"version": "3.0.3#2"},
                          {"version": "3.0.4"}, {"baseline": BASELINE}):
            with self.subTest(overrides=overrides):
                errors = third_party.policy_failures(
                    [self.pkgconf_record(**overrides)], self.repository_policy, self.vcpkg)
                self.assertEqual(len(errors), 1)
                self.assertIn("pkgconf:x64-linux", errors[0])
                self.assertIn("block-unresolved-licences", errors[0])

    def test_pkgconf_exception_does_not_cover_other_unknown_licences(self):
        cases = (
            (self.pkgconf_record(port="other-tool"), "block-unresolved-licences"),
            (self.pkgconf_record(expression="NOASSERTION"), "block-unresolved-licences"),
            (self.pkgconf_record(expression=None), "require-effective-licenseConcluded"),
            (self.pkgconf_record(port="other-tool", expression="LicenseRef-pkgconf-COPYING"),
             "block-unlisted-licences"),
        )
        for record, rule in cases:
            with self.subTest(port=record["port"], expression=record["expression"]):
                errors = third_party.policy_failures(
                    [record], self.repository_policy, self.vcpkg)
                self.assertEqual(len(errors), 1)
                self.assertIn(rule, errors[0])

    def test_pkgconf_exception_requires_a_build_only_dependency_path(self):
        for roles in (["build", "target"], ["target"]):
            with self.subTest(roles=roles):
                errors = third_party.policy_failures(
                    [self.pkgconf_record(roles=roles)], self.repository_policy, self.vcpkg)
                self.assertEqual(len(errors), 1)
                self.assertIn("block-unresolved-licences", errors[0])
        record = self.pkgconf_record(roles=["target"])
        errors = third_party.policy_failures(
            [record], self.pkgconf_policy(roles=["target"]), self.vcpkg)
        self.assertEqual(len(errors), 1)
        self.assertIn("requires a build-only dependency path", errors[0])

    def test_host_tool_exception_requires_the_reviewed_copyright_and_text(self):
        entry = self.pkgconf_entry()
        licence = entry["licence_texts"]["LicenseRef-pkgconf-COPYING"]
        altered = {**licence, "text": licence["text"].replace("'as is'", "\"AS IS\"")}
        cases = (
            (self.pkgconf_record(copyright_sha256="0" * 64), self.repository_policy,
             "does not match the reviewed"),
            (self.pkgconf_record(copyright="COPYING:\n\nDifferent terms.\n"),
             self.repository_policy, "lacks the reviewed LicenseRef-pkgconf-COPYING"),
            (self.pkgconf_record(),
             self.pkgconf_policy(licence_texts={"LicenseRef-pkgconf-COPYING": altered}),
             "does not match its recorded SHA-256"),
            (self.pkgconf_record(),
             self.pkgconf_policy(licence_texts={"LicenseRef-vcpkg-null": licence}),
             "not a reviewable LicenseRef identifier"),
            (self.pkgconf_record(), self.pkgconf_policy(reviewer=""),
             "requires a recorded reviewer"),
            (self.pkgconf_record(), self.pkgconf_policy(reviewed_on="1 November 2026"),
             "requires an ISO reviewed_on date"),
            (self.pkgconf_record(), self.pkgconf_policy(classification="shipped tool"),
             "unknown reviewed classification"),
        )
        for record, policy, message in cases:
            with self.subTest(message=message):
                errors = third_party.policy_failures([record], policy, self.vcpkg)
                self.assertEqual(len(errors), 1)
                self.assertIn("reviewed exception evidence failed validation", errors[0])
                self.assertIn(message, errors[0])

    def test_licence_texts_require_the_host_tool_classification(self):
        policy = self.pkgconf_policy()
        del policy["reviewed_exceptions"][0]["classification"]
        errors = third_party.policy_failures([self.pkgconf_record()], policy, self.vcpkg)
        self.assertEqual(len(errors), 1)
        self.assertIn("accepted only for classification", errors[0])

    def test_reviewed_licence_text_cannot_waive_a_denied_licence(self):
        policy = self.pkgconf_policy(
            resolved_expression="LicenseRef-pkgconf-COPYING AND GPL-2.0-or-later")
        errors = third_party.policy_failures([self.pkgconf_record()], policy, self.vcpkg)
        self.assertEqual(len(errors), 1)
        self.assertIn("GPL-2.0-or-later is denied", errors[0])

    def test_inventory_records_the_installed_copyright_hash(self):
        args = third_party.build_parser().parse_args(self.arguments("policy"))
        records, _ = third_party.load_inventory(args)
        self.assertEqual(
            records[0]["copyright_sha256"],
            hashlib.sha256(b"Fixture copyright and licence.").hexdigest())

    def test_stale_inventory_reports_regeneration_command(self):
        output, error = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            result = third_party.main(self.arguments("check"))
        self.assertEqual(result, 1)
        self.assertIn("THIRD_PARTY.md is stale", error.getvalue())
        self.assertIn("Regenerate with:", error.getvalue())
        self.assertIn("third_party.py generate", error.getvalue())
        self.assertIn("Stale output", error.getvalue())

    def test_generation_updates_one_triplet_and_preserves_manual_tool_inventory(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = third_party.main(self.arguments("generate"))
        self.assertEqual(result, 0)
        document = self.output.read_text(encoding="utf-8")
        self.assertIn("Existing platform.", document)
        self.assertIn("| `fixture` | `1.0` | `MIT` | static", document)
        self.assertIn("Fixture fixture dependency.", document)
        self.assertIn("## CI-only tools", document)

    def test_core_only_inventory_reports_only_core_roots(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = third_party.main(self.arguments("generate", features=()))
        self.assertEqual(result, 0)
        document = self.output.read_text(encoding="utf-8")
        self.assertIn("selected dependency roots: `core`.", document)
        self.assertNotIn("selected dependency roots: `core`, `desktop`", document)

    def test_platform_expressions_follow_the_triplet(self):
        cases = (
            ("linux & x64", "x64-linux", True),
            ("linux & x64", "arm64-linux", False),
            ("linux & x64", "arm64-osx", False),
            ("!windows", "arm64-osx", True),
            ("(windows & !uwp) | (linux & x64)", "x64-linux", True),
            ("(windows & !uwp) | (linux & x64)", "arm64-osx", False),
        )
        for expression, triplet, expected in cases:
            with self.subTest(expression=expression, triplet=triplet):
                self.assertEqual(
                    third_party.platform_matches(expression, triplet), expected)
        with self.assertRaises(third_party.InventoryError):
            third_party.platform_matches("linux &", "x64-linux")

    def test_platform_gated_manifest_dependencies_are_skipped_elsewhere(self):
        manifest = {"dependencies": [
            "fixture", {"name": "linux-tool", "host": True, "platform": "linux & x64"}]}
        linux = third_party.manifest_roots(manifest, (), "x64-linux")
        macos = third_party.manifest_roots(manifest, (), "arm64-osx")
        self.assertEqual(sorted(linux), ["fixture", "linux-tool"])
        self.assertEqual(sorted(macos), ["fixture"])

    def test_dependency_closure_marks_host_tools_separately(self):
        self.make_package(
            "build-helper", "2.0", "MIT", "https://example.test/build-helper")
        helper_manifest = self.vcpkg / "ports" / "fixture" / "vcpkg.json"
        helper_port = json.loads(helper_manifest.read_text(encoding="utf-8"))
        helper_port["dependencies"] = [{"name": "build-helper", "host": True}]
        helper_manifest.write_text(json.dumps(helper_port), encoding="utf-8")
        subprocess.run(
            ["git", "-C", str(self.vcpkg), "add", "ports"], check=True)
        subprocess.run(
            ["git", "-C", str(self.vcpkg), "commit", "-qm", "add build helper"], check=True)
        self.write_manifest()
        status = self.installed / "vcpkg" / "status"
        status.write_text(
            "Package: fixture\nVersion: 1.0\nArchitecture: x64-linux\n"
            "Depends: build-helper\nStatus: install ok installed\n\n"
            "Package: build-helper\nVersion: 2.0\nArchitecture: x64-linux\n"
            "Status: install ok installed\n",
            encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()):
            result = third_party.main(self.arguments("generate"))
        self.assertEqual(result, 0)
        document = self.output.read_text(encoding="utf-8")
        self.assertIn(
            "| `build-helper` | `2.0` | `MIT` | static | core, desktop | build tool |",
            document)


if __name__ == "__main__":
    unittest.main()
