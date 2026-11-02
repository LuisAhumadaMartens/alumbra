import json
import pathlib
import unittest

import yaml

from require_ci_jobs import FOUNDATION_JOBS


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

    def test_foundation_jobs_always_run_and_gate_ci_ok(self):
        jobs = self.workflow["jobs"]
        for name in FOUNDATION_JOBS:
            with self.subTest(job=name):
                self.assertNotIn("if", jobs[name])
                self.assertNotIn("continue-on-error", jobs[name])
                self.assertIn(name, jobs["ci-ok"]["needs"])

    def test_conformance_manifest_is_validated_offline(self):
        job = self.workflow["jobs"]["conformance"]
        steps = {step["name"]: step for step in job["steps"]}
        self.assertEqual(
            steps["Validate the manifest and attribution page"]["run"],
            "python3 tools/conformance/assets.py validate\n"
            "python3 tools/conformance/assets.py attribution --check\n")
        tests = steps["Test the manifest tools"]
        self.assertEqual(tests["env"]["ALUMBRA_REQUIRE_PYYAML"], "1")
        self.assertIn("unittest discover -s tools/conformance", tests["run"])
        generated = steps["Generate and verify synthetic clips offline"]
        self.assertEqual(generated["env"]["ALUMBRA_ASSET_OFFLINE"], "1")
        self.assertEqual(generated["run"].count("fetch --tags generated"), 2)
        self.assertIn("grep -v ': cached '", generated["run"])
        self.assertNotIn("probe", " ".join(step.get("run", "") for step in job["steps"]))

    def test_remote_vectors_run_in_a_separate_non_blocking_job(self):
        jobs = self.workflow["jobs"]
        remote = jobs["conformance-remote"]
        self.assertEqual(remote["continue-on-error"], "true")
        self.assertNotIn("conformance-remote", jobs["ci-ok"]["needs"])
        self.assertNotIn("conformance-remote", FOUNDATION_JOBS)
        gate = next(step for step in jobs["ci-ok"]["steps"] if "env" in step)
        self.assertNotIn("conformance-remote", " ".join(gate["env"].values()))
        commands = [step.get("run", "") for step in remote["steps"]]
        self.assertIn("python3 tools/conformance/assets.py probe", commands)
        self.assertTrue(any("fetch --kind download" in command for command in commands))

    def test_licence_inventory_covers_each_supported_native_triplet(self):
        job = self.workflow["jobs"]["licence"]
        self.assertNotIn("if", job)
        self.assertIn("licence", self.workflow["jobs"]["ci-ok"]["needs"])
        targets = {entry["triplet"] for entry in job["strategy"]["matrix"]["include"]}
        self.assertEqual(targets, {"x64-linux", "arm64-osx"})
        commands = [step.get("run", "") for step in job["steps"]]
        install = next(command for command in commands
                       if "vcpkg install" in command)
        inventory = next(command for command in commands
                         if "third_party.py check" in command)
        policy = next(command for command in commands
                      if "third_party.py policy" in command)
        self.assertIn("--x-feature=desktop --x-feature=tests", install)
        self.assertIn("--x-no-default-features", install)
        self.assertLess(commands.index(install), commands.index(inventory))
        self.assertLess(commands.index(inventory), commands.index(policy))
        gate = next(
            step for step in self.workflow["jobs"]["ci-ok"]["steps"]
            if step.get("name") ==
            "Require every job to succeed or be skipped by the path filter")
        self.assertIn("${{ needs.licence.result }}",
                      gate["env"]["FOUNDATION_RESULTS"])

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
            " ".join(f"${{{{ needs.{job}.result }}}}" for job in FOUNDATION_JOBS))
        self.assertEqual(
            gate["env"]["SANITIZERS_RESULT"],
            "${{ needs.sanitizers.result }}")

    def test_design_token_jobs_are_classified_pinned_and_required(self):
        jobs = self.workflow["jobs"]
        self.assertEqual(jobs["changes"]["outputs"]["tokens"],
                         "${{ steps.classify.outputs.tokens }}")
        for name in ("tokens", "tokens_swift"):
            with self.subTest(job=name):
                self.assertEqual(jobs[name]["needs"], "changes")
                self.assertEqual(jobs[name]["if"],
                                 "needs.changes.outputs.tokens == 'true'")
                self.assertIn(name, jobs["ci-ok"]["needs"])
        gate = next(
            step for step in jobs["ci-ok"]["steps"]
            if step.get("name") ==
            "Require every job to succeed or be skipped by the path filter")
        self.assertEqual(gate["env"]["TOKENS_NEEDED"],
                         "${{ needs.changes.outputs.tokens }}")
        self.assertEqual(gate["env"]["TOKENS_RESULT"], "${{ needs.tokens.result }}")
        self.assertEqual(gate["env"]["TOKENS_SWIFT_RESULT"],
                         "${{ needs.tokens_swift.result }}")
        steps = {step["name"]: step for step in jobs["tokens"]["steps"]}
        self.assertRegex(steps["Set up the pinned Node.js"]["with"]["node-version"],
                         r"^\d+\.\d+\.\d+$")
        self.assertRegex(steps["Set up the pinned JDK"]["with"]["java-version"],
                         r"^\d+\.\d+\.\d+\+\d+$")
        kotlin = steps["Install the pinned Kotlin compiler"]
        self.assertRegex(kotlin["env"]["KOTLIN_VERSION"], r"^\d+\.\d+\.\d+$")
        self.assertRegex(kotlin["env"]["KOTLIN_SHA256"], r"^[0-9a-f]{64}$")
        self.assertIn("sha256sum --check --strict", kotlin["run"])
        commands = "\n".join(step.get("run", "") for step in jobs["tokens"]["steps"])
        for command in (
                "npm ci --ignore-scripts",
                "python3 -m unittest discover -s tools/design -p 'test_*.py'",
                "python3 tools/design/generate_tokens.py --check",
                "npm run typecheck", "npm run lint:css", "kotlinc -Werror",
                "python3 tools/design/golden_tokens.py --css",
                "tools/design/golden/dump.ts", "-jar \"$RUNNER_TEMP/design-tokens.jar\""):
            self.assertIn(command, commands)
        swift = jobs["tokens_swift"]
        self.assertEqual(swift["runs-on"], "macos-26")
        self.assertEqual(swift["env"]["DEVELOPER_DIR"],
                         "/Applications/Xcode_26.6.app/Contents/Developer")
        swift_commands = "\n".join(step.get("run", "") for step in swift["steps"])
        self.assertIn(
            "swiftc -typecheck -warnings-as-errors antumbra/generated/DesignTokens.swift",
            swift_commands)
        self.assertIn("python3 tools/design/golden_tokens.py --run", swift_commands)

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

    def test_cpp_reference_is_classified_pinned_and_required_by_ci_ok(self):
        jobs = self.workflow["jobs"]
        job = jobs["cpp-reference"]
        self.assertEqual(job["needs"], "changes")
        self.assertEqual(job["if"],
                         "needs.changes.outputs.cpp_reference == 'true'")
        self.assertEqual(jobs["changes"]["outputs"]["cpp_reference"],
                         "${{ steps.classify.outputs.cpp_reference }}")
        self.assertIn("cpp-reference", jobs["ci-ok"]["needs"])
        commands = [step.get("run", "") for step in job["steps"]]
        install = next(index for index, command in enumerate(commands)
                       if "tools/cpp-reference/doxygen.py --install-dir"
                       in command)
        tests = commands.index(
            "python3 -m unittest discover -s tools/cpp-reference -p 'test_*.py'")
        build = commands.index(
            'python3 tools/cpp-reference/build_reference.py --output '
            '"$RUNNER_TEMP/api/cpp"')
        self.assertLess(install, tests)
        self.assertLess(tests, build)
        gate = next(
            step for step in jobs["ci-ok"]["steps"]
            if step.get("name") ==
            "Require every job to succeed or be skipped by the path filter")
        self.assertEqual(gate["env"]["CPP_REFERENCE_NEEDED"],
                         "${{ needs.changes.outputs.cpp_reference }}")
        self.assertEqual(gate["env"]["CPP_REFERENCE_RESULT"],
                         "${{ needs.cpp-reference.result }}")

    def test_fuzz_smoke_is_classified_pinned_and_required_by_ci_ok(self):
        jobs = self.workflow["jobs"]
        fuzz = jobs["fuzz"]
        self.assertEqual(fuzz["needs"], "changes")
        self.assertEqual(fuzz["if"], "needs.changes.outputs.cxx == 'true'")
        self.assertEqual(fuzz["runs-on"], "ubuntu-26.04")
        self.assertEqual(fuzz["env"]["LLVM_MAJOR"],
                         "${{ needs.changes.outputs.llvm_major }}")
        self.assertIn("detect_leaks=1", fuzz["env"]["ASAN_OPTIONS"])
        self.assertIn("halt_on_error=1", fuzz["env"]["UBSAN_OPTIONS"])
        self.assertIn("fuzz", jobs["ci-ok"]["needs"])
        gate = next(
            step for step in jobs["ci-ok"]["steps"]
            if step.get("name") ==
            "Require every job to succeed or be skipped by the path filter")
        self.assertEqual(gate["env"]["FUZZ_RESULT"],
                         "${{ needs.fuzz.result }}")
        steps = fuzz["steps"]
        commands = [step.get("run", "") for step in steps]
        install = next(command for command in commands
                       if "libclang-rt-" in command)
        self.assertIn('"clang-$LLVM_MAJOR" "libclang-rt-$LLVM_MAJOR-dev"',
                      install)
        self.assertIn('echo "CXX=clang++-$LLVM_MAJOR"', install)
        configure = commands.index(
            "cmake --preset fuzz -DVCPKG_TARGET_TRIPLET=x64-linux-asan")
        build = commands.index("cmake --build --preset fuzz")
        smoke = next(index for index, step in enumerate(steps)
                     if step.get("run", "").startswith("ctest --preset fuzz ")
                     and "-L" not in step["run"])
        canary_configure = commands.index(
            "cmake --preset fuzz -DVCPKG_TARGET_TRIPLET=x64-linux-asan "
            "-DALUMBRA_FUZZ_CANARIES=ON")
        canary_build = commands.index(
            "cmake --build --preset fuzz --target alumbra_fuzz_canaries")
        canaries = next(index for index, command in enumerate(commands)
                        if command.startswith(
                            "ctest --preset fuzz -L fuzz_canary "))
        self.assertLess(configure, build)
        self.assertLess(build, smoke)
        self.assertLess(smoke, canary_configure)
        self.assertLess(canary_configure, canary_build)
        self.assertLess(canary_build, canaries)
        reports = "${{ runner.temp }}/reports/fuzz"
        self.assertEqual(steps[smoke]["env"]["ALUMBRA_FUZZ_REPORT_DIR"],
                         reports)
        self.assertEqual(steps[canaries]["env"]["ALUMBRA_FUZZ_REPORT_DIR"],
                         reports)
        upload = next(step for step in steps
                      if step.get("uses", "").startswith(
                          "actions/upload-artifact@"))
        self.assertEqual(
            upload["uses"],
            "actions/upload-artifact@cf430e030ddbb5b0abf93d22962f4752f3646cd9")
        self.assertEqual(upload["if"], "always()")
        self.assertEqual(upload["with"]["path"], "${{ runner.temp }}/reports")
        self.assertIn("summarize_fuzz.py", "\n".join(commands))

    def test_windows_engine_job_is_classified_pinned_and_required_by_ci_ok(self):
        jobs = self.workflow["jobs"]
        windows = jobs["windows"]
        self.assertEqual(windows["needs"], "changes")
        self.assertEqual(windows["if"], "needs.changes.outputs.cxx == 'true'")
        self.assertEqual(windows["runs-on"], "windows-2025-vs2026")
        self.assertEqual(windows["defaults"]["run"]["shell"], "bash")
        self.assertEqual(windows["env"]["TRIPLET"], "x64-windows")
        llvm_major = (pathlib.Path(__file__).resolve().parent
                      / "llvm-major").read_text().strip()
        self.assertRegex(windows["env"]["LLVM_VERSION"],
                         rf"\A{llvm_major}\.[0-9]+\.[0-9]+\Z")
        self.assertRegex(windows["env"]["LLVM_SHA256"], r"\A[0-9a-f]{64}\Z")
        self.assertIn("windows", jobs["ci-ok"]["needs"])
        gate = next(
            step for step in jobs["ci-ok"]["steps"]
            if step.get("name") ==
            "Require every job to succeed or be skipped by the path filter")
        self.assertEqual(gate["env"]["WINDOWS_RESULT"],
                         "${{ needs.windows.result }}")
        steps = windows["steps"]
        commands = [step.get("run", "") for step in steps]
        self.assertIn("sha256sum --check --strict", "\n".join(commands))
        install = next(index for index, command in enumerate(commands)
                       if command.startswith("../vcpkg/vcpkg install "))
        self.assertIn("--x-feature=desktop --x-feature=tests",
                      commands[install])
        self.assertIn("--x-no-default-features", commands[install])
        configure = commands.index("cmake --preset windows-debug")
        self.assertLess(install, configure)
        self.assertEqual(
            [command for command in commands
             if command.startswith(("cmake --build", "ctest"))],
            ["cmake --build --preset windows-debug",
             'ctest --preset windows-debug --output-junit '
             '"$RUNNER_TEMP/reports/junit/debug.xml"',
             "cmake --build --preset windows-release",
             'ctest --preset windows-release --output-junit '
             '"$RUNNER_TEMP/reports/junit/release.xml"'])
        save = next(step for step in steps
                    if step.get("name") == "Save the vcpkg binary cache")
        self.assertLess(steps.index(save), steps.index(
            next(step for step in steps
                 if step.get("run") == "cmake --preset windows-debug")))

    def test_windows_presets_build_the_engine_only_with_the_overlay_triplet(self):
        presets = json.loads((pathlib.Path(__file__).resolve().parents[2]
                              / "CMakePresets.json").read_text())
        configure = {preset["name"]: preset
                     for preset in presets["configurePresets"]}
        base = configure["windows"]
        self.assertEqual(base["generator"], "Ninja")
        self.assertEqual(base["condition"]["rhs"], "Windows")
        self.assertEqual(base["cacheVariables"]["ALUMBRA_PORT"], "none")
        self.assertEqual(base["cacheVariables"]["VCPKG_TARGET_TRIPLET"],
                         "x64-windows")
        self.assertNotIn("CMAKE_CXX_COMPILER", base["cacheVariables"])
        for name, build_type in (("windows-debug", "Debug"),
                                 ("windows-release", "Release")):
            self.assertEqual(configure[name]["inherits"], "windows")
            self.assertEqual(
                configure[name]["cacheVariables"]["CMAKE_BUILD_TYPE"],
                build_type)
        triplet = (pathlib.Path(__file__).resolve().parents[2]
                   / "cmake/triplets/x64-windows.cmake").read_text()
        self.assertIn("VCPKG_LIBRARY_LINKAGE static", triplet)
        self.assertIn('PORT STREQUAL "ffmpeg"', triplet)

    def test_fuzz_preset_builds_and_runs_only_the_fuzz_targets(self):
        presets = json.loads((pathlib.Path(__file__).resolve().parents[2]
                              / "CMakePresets.json").read_text())

        def named(kind):
            return next(preset for preset in presets[kind]
                        if preset["name"] == "fuzz")

        configure = named("configurePresets")
        self.assertEqual(configure["inherits"], "asan")
        self.assertIs(configure["cacheVariables"]["ALUMBRA_FUZZING"], True)
        self.assertEqual(configure["cacheVariables"]["ALUMBRA_PORT"], "none")
        self.assertEqual(named("buildPresets")["targets"], ["alumbra_fuzz"])
        test = named("testPresets")
        self.assertEqual(test["filter"]["include"]["label"], "^fuzz_")
        self.assertEqual(test["execution"]["noTestsAction"], "error")
        self.assertEqual(
            [step["type"] for step in named("workflowPresets")["steps"]],
            ["configure", "build", "test"])

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

    def test_cpp_reference_is_built_and_published_under_its_route(self):
        steps = self.workflow["jobs"]["site"]["steps"]
        names = [step["name"] for step in steps]
        assemble = steps[names.index("Assemble the Pages artefact")]
        self.assertIn('--subtree "api/cpp=$RUNNER_TEMP/api/cpp"',
                      assemble["run"])
        build = steps[names.index("Build the C++ API reference")]
        self.assertIn("tools/cpp-reference/build_reference.py", build["run"])
        self.assertLess(names.index("Install pinned Doxygen"),
                        names.index("Build the C++ API reference"))
        self.assertLess(names.index("Build the C++ API reference"),
                        names.index("Assemble the Pages artefact"))

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
