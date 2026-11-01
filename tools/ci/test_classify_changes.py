import contextlib
import io
import os
import pathlib
import subprocess
import tempfile
import unittest
from unittest import mock

import classify_changes

FULL = "cxx=true\ncpp_reference=true\n"
CODE = "cxx=true\ncpp_reference=false\n"
DOCS = "cxx=false\ncpp_reference=false\n"

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
                        self.assertEqual(self.output(), CODE)

    def test_literal_unicode_and_newline_documentation_paths(self):
        names = [
            "docs/caf\u00e9.md", "docs/line\nbreak.md", "docs/tab\tname.md",
            "docs/carriage\rreturn.md",
        ]
        for name in names:
            self.write(name)
        paths = self.compare(self.commit())
        self.assertCountEqual(paths, names)
        self.assertEqual(self.output(), DOCS)

    def test_newline_code_path_cannot_be_split_into_docs_paths(self):
        name = "docs/guide\nsrc/probe.cc"
        self.write(name)
        self.assertEqual(self.compare(self.commit()), [name])
        self.assertEqual(self.output(), DOCS)
        self.write("src/probe\nREADME.md")
        self.compare(self.commit())
        self.assertEqual(self.output(), CODE)

    def test_existing_path_filters(self):
        docs = [
            ".github/CODEOWNERS", ".github/labels.yml",
            ".github/pull_request_template.md", "LICENSE", "NOTICE",
            "docs/guide.md", ".github/ISSUE_TEMPLATE/bug.yml",
            "CONTRIBUTING.md", "umbra/README.md",
        ]
        code = [
            "docs/cpp-features.md", ".github/workflows/ci.yml",
            "CMakeLists.txt", "umbra/example.cc", "tools/ci/check_action_pins.py",
            "cmake/triplets/x64-linux-asan.cmake",
            "test/sanitizers/canary.cc",
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
        self.assertEqual(self.output(), DOCS)
        self.write("CMakeLists.txt")
        self.compare(self.commit())
        self.assertEqual(self.output(), CODE)

    def test_reference_inputs_run_the_reference_job(self):
        reference = [
            "docs/api/cpp/index.md", "docs/api/cpp/umbra/base/base.md",
            "penumbra/interface/include/penumbra/interface/rational.h",
            "umbra/base/include/umbra/base/url_redactor.h",
            "tools/cpp-reference/Doxyfile", "tools/cpp-reference/doxygen.py",
            "test/consumer/examples/media_time.cc", "errors/catalogue.toml",
            "tools/errors/generate.py", ".github/workflows/ci.yml",
        ]
        unrelated = [
            "docs/architecture/overview.md", "README.md", "CMakeLists.txt",
            "umbra/base/internal_include/umbra/base/status.h",
            "umbra/base/status.cc", "test/consumer/main.cc",
            "settings/schema/abr.toml", "include/stray.h",
        ]
        for path in reference:
            with self.subTest(path=path):
                self.assertTrue(classify_changes.is_reference_input(path))
        for path in unrelated:
            with self.subTest(path=path):
                self.assertFalse(classify_changes.is_reference_input(path))
        self.write("docs/api/cpp/index.md")
        self.compare(self.commit())
        self.assertEqual(self.output(), "cxx=false\ncpp_reference=true\n")
        self.write("umbra/base/include/umbra/base/base.h")
        self.compare(self.commit())
        self.assertEqual(self.output(), FULL)

    def test_empty_failed_and_uncomparable_diffs_run_everything(self):
        self.assertEqual(self.compare(self.base), [])
        self.assertEqual(self.output(), FULL)
        with contextlib.redirect_stderr(io.StringIO()) as errors:
            self.assertIsNone(self.compare("invalid-revision"))
        self.assertIn("invalid-revision", errors.getvalue())
        self.assertEqual(self.output(), FULL)
        for event in ("workflow_dispatch", "push", ""):
            with self.subTest(event=event):
                os.environ.update(EVENT=event, BEFORE=classify_changes.ZERO_SHA)
                self.assertIsNone(classify_changes.changed_files())
                self.assertEqual(self.output(), FULL)


if __name__ == "__main__":
    unittest.main()
