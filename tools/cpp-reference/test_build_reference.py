import hashlib
import io
import shutil
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import build_reference
import doxygen
import sidecar_filter

FIXTURES = Path(__file__).resolve().parent / "fixtures"
PREAMBLE = "\\file fixture/widget.h\nHeader.\n\n"


class SidecarFilterTest(unittest.TestCase):
    def test_entries_open_blocks_on_their_own_lines(self):
        text = "\\file a.h\nHeader.\n\n\\fn void f()\nRuns.\n"
        lines = sidecar_filter.to_comment_blocks(text).splitlines()
        self.assertEqual(len(lines), len(text.splitlines()) + 1)
        self.assertEqual(lines[0], "/** \\file a.h")
        self.assertEqual(lines[1], " * Header.")
        self.assertEqual(lines[3], "*/ /** \\fn void f()")
        self.assertEqual(lines[-1], " */")


class LintTest(unittest.TestCase):
    def lint(self, text):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sidecar.md"
            path.write_text(text, encoding="utf-8")
            return build_reference.lint(path)

    def test_accepts_the_fixture_and_inline_code(self):
        path = FIXTURES / "sidecars/fixture/widget.md"
        self.assertEqual(build_reference.lint(path), [])
        self.assertEqual(self.lint(PREAMBLE + "Uses `std::optional`.\n\n- Item\n  continued.\n"), [])

    def test_rejects_uncompiled_code(self):
        for snippet in ("```cpp\nint x;\n```\n", "~~~\nint x;\n~~~\n",
                        "    int x;\n", "\tint x;\n",
                        "\\code\nint x;\n\\endcode\n", "\\snippet example.cc tag\n",
                        "\\dontinclude example.cc\n", "\\skip main\n",
                        "\\verbatim\nint x;\n\\endverbatim\n"):
            with self.subTest(snippet=snippet):
                self.assertTrue(self.lint(PREAMBLE + snippet))

    def test_rejects_misplaced_entries(self):
        for text in ("Prose first.\n\\file fixture/widget.h\n",
                     PREAMBLE + "See \\fn void f() inline.\n",
                     PREAMBLE + "Closes */ a comment.\n", ""):
            with self.subTest(text=text):
                self.assertTrue(self.lint(text))


class EntryNameTest(unittest.TestCase):
    def test_reads_qualified_names(self):
        cases = (
            ("fn", "template <typename Sink> void penumbra::AbslStringify("
             "Sink& sink, StreamType type)", "penumbra::AbslStringify"),
            ("fn", "constexpr auto penumbra::TimeDelta::operator<=>(TimeDelta, "
             "TimeDelta)", "penumbra::TimeDelta::operator<=>"),
            ("fn", "constexpr TimeDelta penumbra::TimeDelta::operator-("
             "TimeDelta value)", "penumbra::TimeDelta::operator-"),
            ("fn", "SharedBytes& penumbra::SharedBytes::operator=("
             "const SharedBytes&)", "penumbra::SharedBytes::operator="),
            ("fn", "static constexpr std::optional<Rational> "
             "penumbra::Rational::Create(int64_t numerator, int64_t denominator)",
             "penumbra::Rational::Create"),
            ("fn", "penumbra::SharedBytes::SharedBytes(SharedBytes&& other) "
             "noexcept", "penumbra::SharedBytes::SharedBytes"),
            ("fn", "virtual std::unique_ptr<ThreadHandle> "
             "penumbra::ThreadFactory::StartThread(std::string_view name, "
             "std::function<void()> body)=0",
             "penumbra::ThreadFactory::StartThread"),
            ("var", "static constexpr int64_t penumbra::Timestamp::kUnknown",
             "penumbra::Timestamp::kUnknown"),
            ("var", "penumbra::Rounding::kUp", "penumbra::Rounding::kUp"),
            ("typedef", "penumbra::SeekTicket", "penumbra::SeekTicket"),
            ("class", "penumbra::Rational", "penumbra::Rational"),
            ("file", "penumbra/interface/rational.h",
             "penumbra/interface/rational.h"),
            ("fn", "no parameter list", None),
        )
        for kind, argument, expected in cases:
            with self.subTest(argument=argument):
                self.assertEqual(build_reference.entry_name(kind, argument),
                                 expected)


class PublishTest(unittest.TestCase):
    def test_replaces_only_an_earlier_reference(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            html = root / "html"
            html.mkdir()
            (html / "index.html").write_text("new")
            (html / "doxygen.css").write_text("css")
            output = root / "api/cpp"
            build_reference.publish(html, output)
            (output / "stale.html").write_text("stale")
            build_reference.publish(html, output)
            self.assertFalse((output / "stale.html").exists())
            self.assertEqual((output / "index.html").read_text(), "new")
            unrelated = root / "unrelated"
            unrelated.mkdir()
            (unrelated / "keep.txt").write_text("keep")
            with self.assertRaises(RuntimeError):
                build_reference.publish(html, unrelated)
            self.assertTrue((unrelated / "keep.txt").exists())


class DoxygenInstallTest(unittest.TestCase):
    def archive(self, directory, member):
        payload = b"#!/bin/sh\necho 1.18.0\n"
        path = Path(directory) / "doxygen.tar.gz"
        with tarfile.open(path, "w:gz") as bundle:
            info = tarfile.TarInfo(member)
            info.size = len(payload)
            bundle.addfile(info, io.BytesIO(payload))
        return path, hashlib.sha256(path.read_bytes()).hexdigest()

    def test_verifies_the_checksum_before_installing(self):
        member = "doxygen-1.18.0/bin/doxygen"
        with tempfile.TemporaryDirectory() as directory:
            path, digest = self.archive(directory, member)
            for expected, succeeds in ((digest, True), ("0" * 64, False)):
                target = Path(directory) / f"install-{succeeds}"
                pins = {("Linux", "x86_64"): (path.as_uri(), expected, member)}
                with self.subTest(succeeds=succeeds), \
                        mock.patch.dict(doxygen.ARCHIVES, pins, clear=True):
                    if succeeds:
                        binary = doxygen.install(target, "Linux", "x86_64")
                        self.assertTrue(binary.is_file())
                    else:
                        with self.assertRaisesRegex(RuntimeError, "SHA-256"):
                            doxygen.install(target, "Linux", "x86_64")
                        self.assertFalse((target / doxygen.VERSION / "doxygen")
                                         .exists())

    def test_rejects_unpinned_platforms(self):
        with self.assertRaisesRegex(RuntimeError, "No pinned Doxygen"):
            doxygen.archive_for("Windows", "AMD64")


class CoverageGateTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.doxygen = build_reference.find_doxygen()

    def build(self, sidecar=None, header=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shutil.copytree(FIXTURES / "include", root / "include")
            shutil.copytree(FIXTURES / "sidecars", root / "sidecars")
            (root / "examples").mkdir()
            (root / "work").mkdir()
            for path, edit in ((root / "sidecars/fixture/widget.md", sidecar),
                               (root / "include/fixture/widget.h", header)):
                if edit:
                    path.write_text(edit(path.read_text()))
            inputs = build_reference.Inputs(
                headers=(root / "include",), sidecars=root / "sidecars",
                examples=root / "examples", excluded_symbols="")
            return build_reference.build(inputs, self.doxygen, root / "work")

    def assertFailsWith(self, errors, *fragments):
        self.assertTrue(errors, "the coverage gate accepted the change")
        text = "\n".join(errors)
        for fragment in fragments:
            self.assertIn(fragment, text)

    def test_complete_fixture_passes(self):
        self.assertEqual(self.build(), [])

    def test_declaration_without_entry_fails(self):
        errors = self.build(header=lambda text: text.replace(
            "int Scale(int value);", "int Scale(int value);\nint Unlisted();"))
        self.assertFailsWith(errors, "Member Unlisted() (function) of "
                             "namespace fixture is not documented")

    def test_removed_entry_fails(self):
        errors = self.build(sidecar=lambda text: text.replace(
            "\\fn int fixture::Widget::Size() const\nReturns the size.\n", ""))
        self.assertFailsWith(errors, "Member Size() const (function) of class "
                             "fixture::Widget is not documented")

    def test_entry_without_text_does_not_document(self):
        errors = self.build(sidecar=lambda text: text.replace(
            "Returns the size.\n", ""))
        self.assertFailsWith(errors, "Size() const", "is not documented")

    def test_entry_for_a_removed_function_fails(self):
        errors = self.build(sidecar=lambda text: text + (
            "\n\\fn int fixture::Removed(int value)\nRemoved.\n"))
        self.assertFailsWith(errors, "fixture::Removed", "was not declared")

    def test_entry_for_a_removed_overload_fails(self):
        errors = self.build(sidecar=lambda text: text + (
            "\n\\fn int fixture::Scale(float value)\nRemoved.\n"))
        self.assertFailsWith(errors, "3 \\fn entries name fixture::Scale")

    def test_entry_for_a_removed_member_overload_fails(self):
        errors = self.build(sidecar=lambda text: text + (
            "\n\\fn void fixture::Widget::Resize(float size)\nRemoved.\n"))
        self.assertFailsWith(errors, "no matching class member found")

    def test_duplicate_entry_fails(self):
        errors = self.build(sidecar=lambda text: text + (
            "\n\\fn int fixture::Widget::Size() const\nAgain.\n"))
        self.assertFailsWith(errors, "2 \\fn entries name fixture::Widget::Size")

    def test_entry_for_a_removed_class_fails(self):
        errors = self.build(sidecar=lambda text: text + (
            "\n\\class fixture::Gadget\nRemoved.\n"))
        self.assertFailsWith(errors, "\\class fixture::Gadget matches no "
                             "declaration")

    def test_entry_for_a_removed_enumerator_fails(self):
        errors = self.build(sidecar=lambda text: text + (
            "\n\\var fixture::Mode::kSlow\nRemoved.\n"))
        self.assertFailsWith(errors, "fixture::Mode::kSlow", "was not declared")

    def test_undocumented_enumerator_fails(self):
        errors = self.build(sidecar=lambda text: text.replace(
            "\\var fixture::Mode::kSafe\nSafe mode.\n", ""))
        self.assertFailsWith(errors, "enumerator fixture::Mode::kSafe is not "
                             "documented")

    def test_undocumented_namespace_fails(self):
        errors = self.build(sidecar=lambda text: text.replace(
            "\\namespace fixture\nNamespace of the coverage fixture.\n", ""))
        self.assertFailsWith(errors, "namespace fixture", "is not documented")

    def test_undocumented_header_fails(self):
        errors = self.build(sidecar=lambda text: text.replace(
            "\\file fixture/widget.h\nHeader used to prove the reference "
            "coverage gate.\n", ""))
        self.assertFailsWith(errors, "widget.h: no \\file entry documents this "
                             "public header")

    def test_example_outside_the_compiled_examples_fails(self):
        errors = self.build(sidecar=lambda text: text.replace(
            "coverage gate.\n", "coverage gate.\n\n\\include widget_example.cc\n"))
        self.assertFailsWith(errors, "widget_example.cc")

    def test_code_block_fails_before_doxygen_runs(self):
        errors = self.build(sidecar=lambda text: text + "\n```cpp\nint x;\n```\n")
        self.assertFailsWith(errors, "code blocks are not allowed")


if __name__ == "__main__":
    unittest.main()
