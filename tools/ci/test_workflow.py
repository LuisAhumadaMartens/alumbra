import pathlib
import unittest

import yaml


class WorkflowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = (pathlib.Path(__file__).resolve().parents[2]
                    / ".github/workflows/ci.yml").read_text()
        cls.workflow = yaml.load(cls.text, Loader=yaml.BaseLoader)

    def test_pull_request_default_activities_and_retargeting(self):
        self.assertCountEqual(
            self.workflow["on"]["pull_request"]["types"],
            ["opened", "synchronize", "reopened", "edited"])
        self.assertNotIn("pull_request_target", self.workflow["on"])

    def test_non_pr_runs_have_unique_groups(self):
        self.assertEqual(
            self.workflow["concurrency"]["group"],
            "ci-${{ github.event.pull_request.number && "
            "format('pr-{0}', github.event.pull_request.number) || "
            "format('run-{0}', github.run_id) }}")
        self.assertEqual(self.workflow["concurrency"]["cancel-in-progress"],
                         "${{ github.event_name == 'pull_request' }}")

    def test_read_only_permissions_and_credentials_are_preserved(self):
        self.assertEqual(self.workflow["permissions"], {"contents": "read"})
        for job in self.workflow["jobs"].values():
            for step in job.get("steps", []):
                if step.get("uses", "").startswith("actions/checkout@"):
                    self.assertEqual(step["with"]["persist-credentials"], "false")

    def test_ci_installs_pinned_parser_and_runs_regressions(self):
        steps = self.workflow["jobs"]["actionlint"]["steps"]
        commands = [step.get("run", "") for step in steps]
        install = commands.index(
            'python3 -m venv "$RUNNER_TEMP/ci-venv"\n'
            '"$RUNNER_TEMP/ci-venv/bin/python" -m pip install -r tools/ci/requirements.txt\n')
        tests = commands.index(
            '"$RUNNER_TEMP/ci-venv/bin/python" -m unittest discover -s tools/ci -p \'test_*.py\'')
        checker = commands.index(
            '"$RUNNER_TEMP/ci-venv/bin/python" tools/ci/check_action_pins.py .github/workflows')
        self.assertLess(install, tests)
        self.assertLess(tests, checker)

    def test_quality_gates_share_the_compiler_pin_and_aggregate(self):
        jobs = self.workflow["jobs"]
        self.assertIn("llvm_major", jobs["changes"]["outputs"])
        for name in ("format", "tidy"):
            self.assertEqual(jobs[name]["needs"], "changes")
            self.assertEqual(jobs[name]["if"],
                             "needs.changes.outputs.cxx == 'true'")
            self.assertIn(name, jobs["ci-ok"]["needs"])
        self.assertIn("FORMAT_RESULT", jobs["ci-ok"]["steps"][0]["env"])
        self.assertIn("TIDY_RESULT", jobs["ci-ok"]["steps"][0]["env"])
        self.assertIn("scripts/format --check", self.text)
        self.assertIn("scripts/tidy -j 2", self.text)
        self.assertNotIn("clang-21", self.text)
        self.assertNotIn("clang++-21", self.text)
        self.assertNotIn("clang-format-21", self.text)
        self.assertNotIn("clang-tidy-21", self.text)

    def test_settings_validation_is_mandatory_and_compares_history(self):
        job = self.workflow["jobs"]["settings"]
        self.assertNotIn("if", job)
        self.assertIn("settings", self.workflow["jobs"]["ci-ok"]["needs"])
        checkout = job["steps"][0]
        self.assertEqual(checkout["with"]["fetch-depth"], "0")
        validation = job["steps"][1]
        self.assertEqual(validation["env"]["BASE"],
                         "${{ github.event.pull_request.base.sha || github.event.before }}")
        self.assertIn('tools/settings/validate.py --previous-ref "$BASE"', validation["run"])
        self.assertIn("0000000000000000000000000000000000000000", validation["run"])
        self.assertIn("python3 tools/settings/validate.py\n", validation["run"])
        self.assertEqual(job["steps"][2]["run"],
                         "python3 -m unittest discover -s tools/settings -p 'test_*.py'")
        self.assertEqual(job["steps"][3]["run"],
                         "python3 tools/settings/fuzz_validate.py --smoke 2000")

    def test_error_catalogue_validation_remains_mandatory(self):
        job = self.workflow["jobs"]["changes"]
        self.assertNotIn("if", job)
        self.assertIn("changes", self.workflow["jobs"]["ci-ok"]["needs"])
        commands = [step.get("run", "") for step in job["steps"]]
        self.assertIn(
            "python3 tools/errors/generate.py --check\n"
            "python3 tools/errors/generate_test.py\n", commands)


if __name__ == "__main__":
    unittest.main()
