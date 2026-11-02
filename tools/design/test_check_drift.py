import pathlib
import shutil
import tempfile
import unittest

import check_drift
import generate_tokens
import token_model

REPOSITORY = pathlib.Path(__file__).resolve().parents[2]


class DriftCheckTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = pathlib.Path(directory.name)
        shutil.copytree(REPOSITORY / "design/tokens", self.root / "design/tokens")
        rendered = generate_tokens.render(token_model.load(self.root / "design/tokens"))
        self.outputs = sorted(rendered)
        for relative, content in rendered.items():
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")

    def test_committed_outputs_pass(self):
        self.assertEqual(check_drift.check(self.root), "")

    def test_hand_edited_output_fails_and_prints_the_command(self):
        for relative in self.outputs:
            with self.subTest(output=relative):
                path = self.root / relative
                original = path.read_text(encoding="utf-8")
                path.write_text(original + "hand edit\n", encoding="utf-8")
                message = check_drift.check(self.root)
                path.write_text(original, encoding="utf-8")
                self.assertIn(relative, message)
                self.assertIn("-hand edit", message)
                self.assertIn("regenerate them with: " + token_model.COMMAND, message)

    def test_missing_output_fails(self):
        (self.root / self.outputs[0]).unlink()
        self.assertIn(self.outputs[0], check_drift.check(self.root))

    def test_token_edit_without_regeneration_fails(self):
        path = self.root / "design/tokens/semantic.tokens.json"
        path.write_text(
            path.read_text(encoding="utf-8").replace("Primary player surface.", "Edited."),
            encoding="utf-8")
        self.assertIn("regenerate them with", check_drift.check(self.root))

    def test_working_tree_is_not_modified(self):
        path = self.root / self.outputs[0]
        path.write_text("hand edit\n", encoding="utf-8")
        check_drift.check(self.root)
        self.assertEqual(path.read_text(encoding="utf-8"), "hand edit\n")


if __name__ == "__main__":
    unittest.main()
