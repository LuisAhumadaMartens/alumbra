import contextlib
import io
import pathlib
import tempfile
import unittest
from unittest import mock

import check_action_pins


PIN = "actions/checkout@" + "a" * 40
DOCKER = "docker://alpine@sha256:" + "b" * 64


class CheckActionPinsTest(unittest.TestCase):
    def check(self, text, suffix=".yml"):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            (root / f"workflow{suffix}").write_text(text, encoding="utf-8")
            out, err = io.StringIO(), io.StringIO()
            with mock.patch("sys.argv", ["check_action_pins.py", temp]), \
                    contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                result = check_action_pins.main()
            return result, out.getvalue(), err.getvalue()

    def steps(self, text):
        return "jobs:\n  test:\n    steps:\n      - uses: " + PIN + "\n" + text

    def test_harmless_scalar_and_comment_text(self):
        text = self.steps(
            '      - name: "Check uses: references"\n'
            '        run: |\n'
            '          uses: actions/checkout@v7\n'
            '          echo "{uses: mutable}"\n'
            '      # uses: actions/checkout@v7\n'
            '      - run: \'echo "uses: mutable"\'\n'
            '      - name: >\n'
            '          Check uses: references\n'
            '        run: echo done\n')
        result, out, err = self.check(text)
        self.assertEqual(result, 0, err)
        self.assertIn("1 'uses:' entries checked, 0 rejected", out)

    def test_pinned_yaml_forms_and_local_actions(self):
        forms = [
            f'      - "uses": "{PIN}"\n',
            f"      - {{uses: '{PIN}'}}\n",
            f"      - uses:\n          {PIN}\n",
            f"      - uses: >-\n          {PIN}\n",
            f"      - uses: '{DOCKER}'\n",
            "      - uses: ./local-action\n",
            f"      - uses: &target {PIN}\n      - uses: *target\n",
            f'      - "us\\u0065s": {PIN}\n',
            f"      - &action {{uses: {PIN}}}\n      - *action\n",
            f"      - <<: {{uses: {PIN}}}\n",
        ]
        for form in forms:
            with self.subTest(form=form):
                result, _, err = self.check(self.steps(form), ".yaml")
                self.assertEqual(result, 0, err)

    def test_mutable_or_invalid_values_cannot_bypass_checker(self):
        forms = [
            '      - "uses": actions/checkout@v7\n',
            '      - "us\\u0065s": actions/checkout@v7\n',
            "      - {uses: actions/checkout@v7}\n",
            "      - uses:\n          actions/checkout@v7\n",
            "      - uses: >-\n          actions/checkout@v7\n",
            "      - uses: |\n          actions/checkout@v7\n",
            "      - uses: docker://alpine:latest\n",
            "      - uses: docker://alpine@sha256:abcd\n",
            "      - uses: actions/checkout@abcd\n",
            "      - uses: ${{ inputs.action }}\n",
            "      - uses:\n",
            "      - uses: true\n",
            "      - uses: 123\n",
            f"      - uses: [{PIN}]\n",
            f"      - uses: {{target: {PIN}}}\n",
            f"      - uses: {PIN}suffix\n",
            f"      - uses: '{PIN} garbage'\n",
            "      - uses: './${{ inputs.action }}'\n",
            "      - &action {uses: actions/checkout@v7}\n      - *action\n",
            "      - <<: {uses: actions/checkout@v7}\n",
            f"      - {{uses: actions/checkout@v7, uses: {PIN}}}\n",
            f"      - uses: !custom {PIN}\n",
        ]
        for form in forms:
            with self.subTest(form=form):
                result, _, err = self.check(self.steps(form))
                self.assertEqual(result, 1)
                self.assertIn("not pinned", err)
                self.assertRegex(err, r"workflow.yml:\d+:")

    def test_reusable_workflow_action_is_checked(self):
        for target, expected in ((PIN, 0), ("owner/repo/.github/workflows/ci.yml@main", 1)):
            with self.subTest(target=target):
                result, _, _ = self.check(f"jobs:\n  test:\n    uses: {target}\n")
                self.assertEqual(result, expected)

    def test_env_and_input_keys_are_not_action_references(self):
        text = (
            "env: {uses: documentation}\n"
            "on: {workflow_dispatch: {inputs: {uses: {default: mode}}}}\n"
            "jobs:\n"
            "  test:\n"
            "    env: {uses: documentation}\n"
            "    steps:\n"
            f"      - uses: {PIN}\n"
            "        env: {uses: documentation}\n"
            "        with: {uses: mode}\n"
            "      - run: echo done\n"
            "        env: {uses: documentation}\n")
        result, out, err = self.check(text)
        self.assertEqual(result, 0, err)
        self.assertIn("1 'uses:' entries checked, 0 rejected", out)

    def test_job_and_step_merges_preserve_unpinned_entries(self):
        forms = [
            "jobs:\n  test:\n    <<: {uses: actions/checkout@v7}\n",
            "jobs:\n  <<: {test: {uses: actions/checkout@v7}}\n",
            "jobs:\n  test:\n    <<: {steps: [{uses: actions/checkout@v7}]}\n",
            "jobs:\n  test:\n    steps:\n"
            f"      - <<: [{{uses: {PIN}}}, {{uses: actions/checkout@v7}}]\n",
            "jobs:\n  test:\n    steps:\n"
            f"      - <<: {{uses: actions/checkout@v7}}\n        uses: {PIN}\n",
            "jobs:\n  test:\n    steps:\n"
            f"      - uses: actions/checkout@v7\n    steps: [{{uses: {PIN}}}]\n",
            "jobs: {test: {uses: actions/checkout@v7}}\n"
            f"jobs: {{test: {{uses: {PIN}}}}}\n",
        ]
        for text in forms:
            with self.subTest(text=text):
                result, _, err = self.check(text)
                self.assertEqual(result, 1)
                self.assertIn("not pinned", err)

    def test_composite_action_steps_are_checked_without_input_false_positives(self):
        for target, expected in ((PIN, 0), ("actions/checkout@v7", 1)):
            with self.subTest(target=target):
                result, _, err = self.check(
                    "inputs: {uses: {description: mode}}\n"
                    "runs:\n  using: composite\n  steps:\n"
                    f"    - uses: {target}\n"
                    "      with: {uses: mode}\n"
                    "      env: {uses: documentation}\n")
                self.assertEqual(result, expected, err)

    def test_invalid_yaml_fails_closed(self):
        for text in (self.steps("      - {uses: broken\n"),
                     self.steps("---\nuses: actions/checkout@v7\n")):
            with self.subTest(text=text):
                result, _, err = self.check(text)
                self.assertEqual(result, 1)
                self.assertIn("invalid YAML", err)

    def test_no_actions_fails_and_scalar_mentions_are_not_actions(self):
        for text in ("", 'name: "Check uses: references"\n', "# uses: mutable\n"):
            with self.subTest(text=text):
                result, _, err = self.check(text)
                self.assertEqual(result, 1)
                self.assertIn("No 'uses:' entries", err)

    def test_recursive_alias_does_not_loop_or_hide_actions(self):
        result, out, err = self.check(
            f"jobs: &jobs\n  cycle: *jobs\n  test:\n    uses: {PIN}\n")
        self.assertEqual(result, 0, err)
        self.assertIn("1 'uses:' entries checked", out)


if __name__ == "__main__":
    unittest.main()
