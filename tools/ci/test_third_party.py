import contextlib
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

    def arguments(self, command):
        return [
            command,
            "--triplet", "x64-linux",
            "--installed-root", str(self.installed),
            "--vcpkg-root", str(self.vcpkg),
            "--manifest", str(self.manifest),
            "--policy", str(self.policy),
            "--output", str(self.output),
            "--feature", "desktop",
            "--feature", "tests",
        ]

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
        self.assertIn("cannot waive denied SPDX terms GPL-3.0-only", errors[0])

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
