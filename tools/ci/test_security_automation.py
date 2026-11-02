import pathlib
import unittest

import yaml

ROOT = pathlib.Path(__file__).resolve().parents[2]
CODEQL = "github/codeql-action/"
ANALYSIS_PERMISSIONS = {"contents": "read", "security-events": "write"}


def load(path):
    return yaml.load((ROOT / path).read_text(), Loader=yaml.BaseLoader)


def steps_using(job, prefix):
    return [step for step in job.get("steps", [])
            if step.get("uses", "").startswith(prefix)]


def step_named(job, name):
    return next(step for step in job["steps"] if step.get("name") == name)


def step_index(job, predicate):
    return next(index for index, step in enumerate(job["steps"])
                if predicate(step))


class CodeqlWorkflowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workflow = load(".github/workflows/codeql.yml")
        cls.jobs = cls.workflow["jobs"]
        cls.ci = load(".github/workflows/ci.yml")["jobs"]
        cls.gcc = next(entry for entry in cls.ci["build"]["strategy"]["matrix"]["include"]
                       if entry["id"] == "linux-gcc")

    def test_runs_on_pull_requests_main_schedule_and_demand(self):
        triggers = self.workflow["on"]
        self.assertIn("pull_request", triggers)
        self.assertNotIn("pull_request_target", triggers)
        self.assertEqual(triggers["push"], {"branches": ["main"]})
        self.assertEqual(len(triggers["schedule"]), 1)
        self.assertEqual(len(triggers["schedule"][0]["cron"].split()), 5)
        self.assertIn("workflow_dispatch", triggers)
        self.assertEqual(self.workflow["concurrency"]["cancel-in-progress"],
                         "${{ github.event_name == 'pull_request' }}")

    def test_only_analysis_jobs_may_upload_results(self):
        self.assertEqual(self.workflow["permissions"], {"contents": "read"})
        for name, job in self.jobs.items():
            with self.subTest(job=name):
                if steps_using(job, CODEQL):
                    self.assertEqual(job["permissions"], ANALYSIS_PERMISSIONS)
                else:
                    self.assertNotIn("permissions", job)

    def test_checkouts_do_not_persist_credentials(self):
        for name, job in self.jobs.items():
            for step in steps_using(job, "actions/checkout@"):
                with self.subTest(job=name, step=step.get("name")):
                    self.assertEqual(step["with"]["persist-credentials"], "false")

    def test_codeql_actions_share_one_full_commit_pin(self):
        references = [step["uses"] for job in self.jobs.values()
                      for step in steps_using(job, CODEQL)]
        self.assertEqual(len(references), 4)
        for reference in references:
            self.assertRegex(
                reference, r"\Agithub/codeql-action/(init|analyze)@[0-9a-f]{40}\Z")
        self.assertEqual(len({reference.split("@")[1] for reference in references}), 1)

    def test_languages_build_modes_suite_and_categories(self):
        expected = {"workflows": ("actions", "none"), "cxx": ("c-cpp", "manual")}
        for name, (language, mode) in expected.items():
            with self.subTest(job=name):
                job = self.jobs[name]
                (init,) = steps_using(job, CODEQL + "init@")
                (analyze,) = steps_using(job, CODEQL + "analyze@")
                self.assertEqual(init["with"]["languages"], language)
                self.assertEqual(init["with"]["build-mode"], mode)
                self.assertEqual(init["with"]["queries"], "security-extended")
                self.assertEqual(analyze["with"]["category"], f"/language:{language}")

    def test_cxx_dependencies_are_installed_before_tracing(self):
        job = self.jobs["cxx"]
        configure = step_index(job, lambda step: "cmake --preset" in step.get("run", ""))
        init = step_index(job, lambda step: step.get("uses", "").startswith(CODEQL + "init@"))
        build = step_index(job, lambda step: "cmake --build" in step.get("run", ""))
        analyze = step_index(
            job, lambda step: step.get("uses", "").startswith(CODEQL + "analyze@"))
        self.assertLess(configure, init)
        self.assertLess(init, build)
        self.assertLess(build, analyze)
        self.assertEqual(
            job["steps"][configure]["run"],
            'cmake --preset debug -B "$ALUMBRA_CODEQL_BUILD_DIR" -DALUMBRA_BUILD_UI=ON -DCMAKE_CXX_SCAN_FOR_MODULES=OFF')
        self.assertEqual(job["steps"][build]["run"],
                         'cmake --build "$ALUMBRA_CODEQL_BUILD_DIR"')

    def test_cxx_build_tree_is_outside_the_analyzed_source_root(self):
        job = self.jobs["cxx"]
        (checkout, _) = steps_using(job, "actions/checkout@")
        (init,) = steps_using(job, CODEQL + "init@")
        (analyze,) = steps_using(job, CODEQL + "analyze@")
        self.assertEqual(checkout["with"]["path"], "src")
        self.assertEqual(init["with"]["source-root"], "src")
        self.assertEqual(analyze["with"]["checkout_path"], "${{ github.workspace }}/src")
        build_dir = job["env"]["ALUMBRA_CODEQL_BUILD_DIR"]
        self.assertTrue(build_dir.startswith("${{ github.workspace }}/"))
        self.assertNotIn(build_dir.split("/")[1], {"src", "vcpkg", "vcpkg-cache"})

    def test_cxx_restores_the_linux_gcc_cache_without_saving_it(self):
        job = self.jobs["cxx"]
        build = self.ci["build"]
        self.assertEqual(steps_using(job, "actions/cache/save@"), [])
        (restore,) = steps_using(job, "actions/cache/restore@")
        source = step_named(build, "Restore the vcpkg binary cache")

        def expand(text):
            return (text.replace("${{ matrix.id }}", self.gcc["id"])
                    .replace("${{ matrix.triplet }}", self.gcc["triplet"]))

        for field in ("path", "key", "restore-keys"):
            with self.subTest(field=field):
                self.assertEqual(restore["with"][field], expand(source["with"][field]))
        self.assertEqual(job["env"]["CC"], self.gcc["cc"])
        self.assertEqual(job["env"]["CXX"], self.gcc["cxx"])
        self.assertEqual(job["runs-on"], self.gcc["runner"])
        for variable in ("VCPKG_ROOT", "VCPKG_BINARY_SOURCES", "VCPKG_DISABLE_METRICS"):
            with self.subTest(variable=variable):
                self.assertEqual(job["env"][variable], build["env"][variable])
        toolchain = step_named(job, "Identify the compiler")["run"]
        self.assertIn('"$CXX" --version > "$RUNNER_TEMP/compiler.txt"', toolchain)
        self.assertIn('shasum -a 256 "$RUNNER_TEMP/compiler.txt" | cut -c1-16', toolchain)
        recorded = step_named(build, "Record the toolchain")["run"]
        self.assertIn('"$CXX" --version', recorded)
        self.assertIn("| cut -c1-16)", recorded)

    def test_pull_requests_skip_cxx_only_for_documentation(self):
        job = self.jobs["cxx"]
        self.assertEqual(job["needs"], "changes")
        self.assertEqual(
            job["if"],
            "github.event_name != 'pull_request' || needs.changes.outputs.cxx == 'true'")
        self.assertNotIn("if", self.jobs["workflows"])
        self.assertNotIn("if", self.jobs["changes"])
        classify = step_named(self.jobs["changes"], "Classify changed paths")
        reference = step_named(self.ci["changes"], "Classify changed paths")
        self.assertEqual(classify["run"], reference["run"])
        self.assertEqual(classify["env"], reference["env"])
        (checkout,) = steps_using(self.jobs["changes"], "actions/checkout@")
        self.assertEqual(checkout["with"]["fetch-depth"], "0")


class DependabotTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = load(".github/dependabot.yml")
        cls.updates = {update["package-ecosystem"]: update
                       for update in cls.config["updates"]}
        labels = yaml.safe_load((ROOT / ".github/labels.yml").read_text())
        cls.labels = {label["name"] for label in labels}

    def test_updates_the_vcpkg_baseline_and_pinned_actions(self):
        self.assertEqual(self.config["version"], "2")
        self.assertEqual(len(self.config["updates"]), 2)
        self.assertEqual(set(self.updates), {"vcpkg", "github-actions"})
        for ecosystem, update in self.updates.items():
            with self.subTest(ecosystem=ecosystem):
                self.assertEqual(update["directory"], "/")
                self.assertEqual(update["schedule"]["interval"], "weekly")
                self.assertNotIn("versioning-strategy", update)

    def test_each_ecosystem_is_grouped_into_one_pull_request(self):
        for ecosystem, update in self.updates.items():
            with self.subTest(ecosystem=ecosystem):
                (group,) = update["groups"].values()
                self.assertEqual(group["patterns"], ["*"])
                self.assertNotIn("exclude-patterns", group)

    def test_labels_are_declared_in_the_label_catalogue(self):
        for ecosystem, update in self.updates.items():
            with self.subTest(ecosystem=ecosystem):
                self.assertIn("type:chore", update["labels"])
                self.assertLessEqual(set(update["labels"]), self.labels)

    def test_vcpkg_ignores_release_tags_not_newer_than_the_baseline(self):
        (ignore,) = self.updates["vcpkg"]["ignore"]
        self.assertEqual(ignore["dependency-name"], "github.com/microsoft/vcpkg")
        (bound,) = ignore["versions"]
        self.assertRegex(bound, r"\A<= \d{4}\.\d{2}\.\d{2}\Z")
        self.assertNotIn("ignore", self.updates["github-actions"])


if __name__ == "__main__":
    unittest.main()
