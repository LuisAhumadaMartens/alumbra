import pathlib
import unittest

import yaml


WORKFLOWS = pathlib.Path(__file__).resolve().parents[2] / ".github/workflows"


def load(name):
    text = (WORKFLOWS / name).read_text()
    return text, yaml.load(text, Loader=yaml.BaseLoader)


class WorkflowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text, cls.workflow = load("ci.yml")

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
        gate = next(
            step for step in jobs["ci-ok"]["steps"]
            if step.get("name") ==
            "Require every job to succeed or be skipped by the path filter")
        self.assertEqual(gate["env"]["FORMAT_RESULT"],
                         "${{ needs.format.result }}")
        self.assertEqual(gate["env"]["TIDY_RESULT"], "${{ needs.tidy.result }}")
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

    def test_sanitizer_legs_are_classified_and_required_by_ci_ok(self):
        jobs = self.workflow["jobs"]
        sanitizer = jobs["sanitizers"]
        self.assertEqual(sanitizer["needs"], "changes")
        self.assertEqual(
            sanitizer["if"], "needs.changes.outputs.cxx == 'true'")
        matrix = sanitizer["strategy"]["matrix"]["include"]
        self.assertCountEqual(
            [entry["triplet"] for entry in matrix],
            ["x64-linux-asan", "x64-linux-tsan", "arm64-osx-asan"])
        self.assertIn("sanitizers", jobs["ci-ok"]["needs"])
        checkout = next(
            step for step in jobs["ci-ok"]["steps"]
            if step.get("name") == "Check out")
        self.assertRegex(checkout["uses"], r"\Aactions/checkout@[0-9a-f]{40}\Z")
        gate = next(
            step for step in jobs["ci-ok"]["steps"]
            if step.get("name") ==
            "Require every job to succeed or be skipped by the path filter")
        self.assertEqual(gate["run"], "python3 tools/ci/require_ci_jobs.py")
        self.assertEqual(
            gate["env"]["FOUNDATION_RESULTS"],
            "${{ needs.changes.result }} "
            "${{ needs.actionlint.result }} ${{ needs.settings.result }}")
        self.assertEqual(
            gate["env"]["SANITIZERS_RESULT"],
            "${{ needs.sanitizers.result }}")

    def test_sanitizer_runtime_options_and_instrumentation_checks(self):
        sanitizer = self.workflow["jobs"]["sanitizers"]
        matrix = sanitizer["strategy"]["matrix"]["include"]
        address = [entry for entry in matrix
                   if entry["sanitizer"] == "address"]
        linux_address = [entry for entry in address
                         if entry["runner"] == "ubuntu-26.04"]
        macos_address = [entry for entry in address
                         if entry["runner"] == "macos-26"]
        thread = [entry for entry in matrix
                  if entry["sanitizer"] == "thread"]
        self.assertEqual(len(address), 2)
        self.assertEqual(len(linux_address), 1)
        self.assertEqual(len(macos_address), 1)
        self.assertEqual(len(thread), 1)
        for entry in linux_address:
            self.assertEqual(
                entry["asan_options"],
                "detect_stack_use_after_return=1:strict_string_checks=1:detect_leaks=1")
            self.assertEqual(
                entry["ubsan_options"],
                "print_stacktrace=1:halt_on_error=1")
        self.assertEqual(
            macos_address[0]["asan_options"],
            "detect_stack_use_after_return=1:strict_string_checks=1")
        self.assertEqual(
            thread[0]["tsan_options"],
            "halt_on_error=1:second_deadlock_stack=1")
        commands = [step.get("run", "") for step in sanitizer["steps"]]
        self.assertTrue(any("verify_sanitizer_instrumentation.py" in command
                            for command in commands))
        self.assertTrue(any("sanitizer_canary" in command
                            for command in commands))

    def test_site_jobs_are_classified_and_required_by_ci_ok(self):
        jobs = self.workflow["jobs"]
        self.assertEqual(jobs["changes"]["outputs"]["site"],
                         "${{ steps.classify.outputs.site }}")
        for name in ("prose", "site"):
            self.assertEqual(jobs[name]["needs"], "changes")
            self.assertEqual(jobs[name]["if"],
                             "needs.changes.outputs.site == 'true'")
            self.assertIn(name, jobs["ci-ok"]["needs"])
        gate = next(
            step for step in jobs["ci-ok"]["steps"]
            if step.get("name") ==
            "Require every job to succeed or be skipped by the path filter")
        self.assertEqual(gate["env"]["SITE_NEEDED"],
                         "${{ needs.changes.outputs.site }}")
        self.assertEqual(gate["env"]["PROSE_RESULT"],
                         "${{ needs.prose.result }}")
        self.assertEqual(gate["env"]["SITE_RESULT"],
                         "${{ needs.site.result }}")

    def test_pages_workflow_is_called_with_only_the_tokens_it_needs(self):
        jobs = self.workflow["jobs"]
        self.assertEqual(jobs["site"]["uses"], "./.github/workflows/pages.yml")
        self.assertEqual(jobs["site"]["permissions"], {
            "contents": "read", "pages": "write", "id-token": "write"})
        for name, job in jobs.items():
            if name != "site":
                self.assertNotIn("pages", job.get("permissions", {}))
                self.assertNotIn("id-token", job.get("permissions", {}))

    def test_prose_checks_use_pinned_tools_and_changed_markdown(self):
        steps = self.workflow["jobs"]["prose"]["steps"]
        install = steps[1]
        for name in ("TYPOS", "VALE", "GOOGLE_STYLE"):
            self.assertRegex(install["env"][f"{name}_SHA256"], r"^[0-9a-f]{64}$")
        self.assertEqual(install["run"].count("sha256sum --check --strict"), 3)
        commands = "\n".join(step.get("run", "") for step in steps)
        self.assertIn("typos --format brief", commands)
        self.assertIn("typos --config .typos-adr.toml", commands)
        self.assertIn("classify_changes.py --changed-markdown", commands)
        self.assertIn("xargs -0 vale", commands)


class PagesWorkflowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text, cls.workflow = load("pages.yml")

    def test_runs_only_when_called_scheduled_or_dispatched(self):
        self.assertCountEqual(self.workflow["on"],
                              ["workflow_call", "workflow_dispatch", "schedule"])
        self.assertEqual(self.workflow["permissions"], {"contents": "read"})

    def test_only_the_deploy_job_can_write_pages(self):
        jobs = self.workflow["jobs"]
        for name, job in jobs.items():
            if name == "deploy":
                self.assertEqual(job["permissions"],
                                 {"pages": "write", "id-token": "write"})
            else:
                self.assertNotIn("permissions", job)
        deploy = jobs["deploy"]
        self.assertEqual(deploy["needs"], "site")
        self.assertIn("github.ref == 'refs/heads/main'", deploy["if"])
        self.assertIn("github.event_name == 'push'", deploy["if"])
        self.assertNotIn("pull_request", deploy["if"])
        self.assertEqual(deploy["environment"]["name"], "github-pages")
        self.assertEqual(deploy["concurrency"],
                         {"group": "pages", "cancel-in-progress": "false"})
        self.assertTrue(deploy["steps"][0]["uses"].startswith(
            "actions/deploy-pages@"))

    def test_site_build_fails_on_warnings_and_broken_internal_links(self):
        steps = self.workflow["jobs"]["site"]["steps"]
        names = [step["name"] for step in steps]
        build = steps[names.index("Build the site and fail on warnings")]
        self.assertIn("set -o pipefail", build["run"])
        self.assertIn("\\[(WARN|ERROR)\\]", build["run"])
        self.assertIn("exit 1", build["run"])
        links = steps[names.index("Check internal links and anchors")]
        self.assertIn("--offline", links["run"])
        self.assertIn("--root-dir", links["run"])
        self.assertLess(names.index("Assemble the Pages artefact"),
                        names.index("Check internal links and anchors"))
        self.assertLess(names.index("Check internal links and anchors"),
                        names.index("Upload the Pages artefact"))
        upload = steps[names.index("Upload the Pages artefact")]
        self.assertTrue(upload["uses"].startswith(
            "actions/upload-pages-artifact@"))

    def test_external_links_are_scheduled_and_never_called_from_ci(self):
        job = self.workflow["jobs"]["external-links"]
        self.assertEqual(job["needs"], "site")
        self.assertEqual(
            job["if"],
            "github.event_name == 'schedule' || inputs.external-links")
        self.assertNotIn("inputs", self.workflow["on"]["workflow_call"] or {})
        run = job["steps"][-1]["run"]
        self.assertNotIn("--offline", run)
        self.assertIn("--exclude", run)

    def test_downloaded_tools_are_checksum_verified(self):
        for job in self.workflow["jobs"].values():
            for step in job.get("steps", []):
                run = step.get("run", "")
                if "curl " in run:
                    self.assertIn("sha256sum --check --strict", run)
                if step.get("uses", "").startswith("actions/checkout@"):
                    self.assertEqual(step["with"]["persist-credentials"], "false")


if __name__ == "__main__":
    unittest.main()
