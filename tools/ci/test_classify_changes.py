import contextlib
import io
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import classify_changes


class ClassifyChangesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.git("init", "-q")
        self.git("config", "user.name", "CI tests")
        self.git("config", "user.email", "ci-tests@example.invalid")
        self.git("config", "core.quotePath", "true")
        self.git("config", "diff.renames", "true")
        self.write("docs/cpp-features.md")
        self.write("src/probe.cc")
        self.base = self.commit()
        self.environment = mock.patch.dict(os.environ, {}, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)
        original = pathlib.Path.cwd()
        os.chdir(self.root)
        self.addCleanup(os.chdir, original)

    def git(self, *args):
        return subprocess.run(
            ["git", *args], cwd=self.root, capture_output=True, text=True,
            check=True).stdout.strip()

    def write(self, path):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text("fixture\n", encoding="utf-8")

    def commit(self):
        self.git("add", "--all")
        self.git("commit", "-qm", "fixture")
        return self.git("rev-parse", "HEAD")

    def compare(self, head, event="pull_request"):
        os.environ.update(EVENT=event, BASE=self.base, HEAD=head,
                          BEFORE=self.base, SHA=head)
        return classify_changes.changed_files()

    def output(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            classify_changes.main()
        return out.getvalue()

    def decisions(self):
        return dict(line.split("=") for line in self.output().splitlines())

    def test_renames_include_deleted_probe_and_code_inputs(self):
        for source, destination in (
                ("docs/cpp-features.md", "docs/features.md"),
                ("src/probe.cc", "docs/probe.md")):
            with self.subTest(source=source):
                self.git("mv", source, destination)
                head = self.commit()
                for event in ("pull_request", "push"):
                    with self.subTest(event=event):
                        paths = self.compare(head, event)
                        self.assertIn(source, paths)
                        self.assertIn(destination, paths)
                        self.assertEqual(self.decisions()["cxx"], "true")

    def test_literal_unicode_and_newline_documentation_paths(self):
        names = [
            "docs/caf\u00e9.md", "docs/line\nbreak.md", "docs/tab\tname.md",
            "docs/carriage\rreturn.md",
        ]
        for name in names:
            self.write(name)
        paths = self.compare(self.commit())
        self.assertCountEqual(paths, names)
        self.assertEqual(self.decisions()["cxx"], "false")

    def test_newline_code_path_cannot_be_split_into_docs_paths(self):
        name = "docs/guide\nsrc/probe.cc"
        self.write(name)
        self.assertEqual(self.compare(self.commit()), [name])
        self.assertEqual(self.decisions()["cxx"], "false")
        self.write("src/probe\nREADME.md")
        self.compare(self.commit())
        self.assertEqual(self.decisions()["cxx"], "true")

    def test_existing_path_filters(self):
        docs = [
            ".github/CODEOWNERS", ".github/labels.yml",
            ".github/pull_request_template.md", "LICENSE", "NOTICE",
            "docs/guide.md", ".github/ISSUE_TEMPLATE/bug.yml",
            "CONTRIBUTING.md", "umbra/README.md", ".typos.toml", ".vale.ini",
            "tools/site/astro.config.mjs", "tools/site/pnpm-lock.yaml",
            "tools/vale/styles/config/vocabularies/Alumbra/accept.txt",
        ]
        code = [
            "docs/cpp-features.md", ".github/workflows/ci.yml",
            "CMakeLists.txt", "umbra/example.cc", "tools/ci/check_action_pins.py",
            "cmake/triplets/x64-linux-asan.cmake",
            "test/sanitizers/canary.cc", ".github/workflows/pages.yml",
        ]
        for path in docs:
            with self.subTest(path=path):
                self.assertTrue(classify_changes.is_docs_only(path))
        for path in code:
            with self.subTest(path=path):
                self.assertFalse(classify_changes.is_docs_only(path))
        for path in docs:
            self.write(path)
        self.compare(self.commit())
        self.assertEqual(self.decisions()["cxx"], "false")
        self.write("CMakeLists.txt")
        self.compare(self.commit())
        self.assertEqual(self.decisions()["cxx"], "true")

    def test_empty_failed_and_uncomparable_diffs_run_everything(self):
        self.assertEqual(self.compare(self.base), [])
        self.assertEqual(self.decisions()["cxx"], "true")
        with contextlib.redirect_stderr(io.StringIO()) as errors:
            self.assertIsNone(self.compare("invalid-revision"))
        self.assertIn("invalid-revision", errors.getvalue())
        self.assertEqual(self.decisions()["cxx"], "true")
        for event in ("workflow_dispatch", "push", ""):
            with self.subTest(event=event):
                os.environ.update(EVENT=event, BEFORE=classify_changes.ZERO_SHA)
                self.assertIsNone(classify_changes.changed_files())
                self.assertEqual(
                    self.decisions(), {"cxx": "true", "site": "true"})

    def test_site_path_filters(self):
        site = [
            "docs/guide.md", "docs/cpp-features.md", "docs/api/cpp/base.md",
            "tools/site/astro.config.mjs", "tools/site/pnpm-lock.yaml",
            "tools/vale/styles/config/vocabularies/Alumbra/accept.txt",
            ".typos.toml", ".vale.ini", "settings/schema/player.toml",
            ".github/workflows/pages.yml", ".github/workflows/ci.yml",
            "tools/ci/classify_changes.py", "README.md", "CONTRIBUTING.md",
            "umbra/README.md", ".github/pull_request_template.md",
            "umbra/base/include/umbra/base/base.h",
            "penumbra/interface/include/penumbra/interface/port_types.h",
        ]
        other = [
            "umbra/base/base.cc",
            "umbra/base/internal_include/umbra/base/status.h",
            "CMakeLists.txt", "cmake/triplets/x64-linux.cmake", "vcpkg.json",
            "test/sanitizers/canary.cc", "test/build/public_header_cxx23/"
            "umbra/fixture/cxx23_only.h", "LICENSE", ".github/labels.yml",
            "tools/errors/generate.py",
        ]
        for path in site:
            with self.subTest(path=path):
                self.assertTrue(classify_changes.is_site_input(path))
        for path in other:
            with self.subTest(path=path):
                self.assertFalse(classify_changes.is_site_input(path))

    def test_cxx_only_changes_skip_the_site_and_docs_run_it(self):
        self.write("umbra/base/base.cc")
        self.write("umbra/base/internal_include/umbra/base/status.h")
        self.compare(self.commit())
        self.assertEqual(self.decisions(), {"cxx": "true", "site": "false"})
        self.write("docs/guide.md")
        self.compare(self.commit())
        self.assertEqual(self.decisions(), {"cxx": "true", "site": "true"})
        self.base = self.git("rev-parse", "HEAD")
        self.write("tools/site/src/site.mjs")
        self.compare(self.commit())
        self.assertEqual(self.decisions(), {"cxx": "false", "site": "true"})
        self.base = self.git("rev-parse", "HEAD")
        self.write("umbra/base/include/umbra/base/base.h")
        self.compare(self.commit())
        self.assertEqual(self.decisions(), {"cxx": "true", "site": "true"})

    def changed_markdown(self):
        out = io.BytesIO()
        stdout = io.TextIOWrapper(out)
        with mock.patch.object(sys, "argv", ["classify", "--changed-markdown"]), \
                contextlib.redirect_stdout(stdout):
            classify_changes.main()
            stdout.flush()
        return [os.fsdecode(path) for path in out.getvalue().split(b"\0") if path]

    def test_changed_markdown_lists_existing_changed_files(self):
        self.write("docs/old.md")
        self.write("docs/kept.md")
        self.base = self.commit()
        self.write("docs/new.md")
        self.write("docs/caf\u00e9.md")
        self.write("umbra/base/base.cc")
        self.git("rm", "-q", "docs/old.md")
        self.compare(self.commit())
        self.assertCountEqual(
            self.changed_markdown(), ["docs/new.md", "docs/caf\u00e9.md"])

    def test_changed_markdown_without_a_base_lists_every_markdown_file(self):
        self.write("docs/guide.md")
        self.write("README.md")
        self.commit()
        os.environ.update(EVENT="workflow_dispatch")
        self.assertCountEqual(
            self.changed_markdown(),
            ["docs/cpp-features.md", "docs/guide.md", "README.md"])


if __name__ == "__main__":
    unittest.main()
