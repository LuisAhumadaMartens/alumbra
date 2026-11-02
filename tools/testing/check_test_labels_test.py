import unittest

import check_test_labels as labels


def test(*names, issue=None):
    properties = {"LABELS": list(names)}
    if issue:
        properties[labels.ISSUE_PROPERTY] = issue
    return properties


class CheckTestLabelsTest(unittest.TestCase):
    def test_exactly_one_kind_is_required(self):
        tests = {"none": test("slow"), "two": test("unit", "golden"),
                 "ok": test("unit", "slow")}
        found = labels.kind_problems(tests)
        self.assertEqual(len(found), 2)
        self.assertTrue(found[0].startswith("none "))
        self.assertTrue(found[1].startswith("two "))

    def test_quarantine_needs_an_issue(self):
        tests = {"bad": test("unit", "quarantine"),
                 "good": test("unit", "quarantine", issue="7")}
        found = labels.quarantine_problems(tests)
        self.assertEqual(found, ["bad is quarantined without a linked issue"])

    def test_unit_selection_must_match_the_label(self):
        tests = {"a": test("unit"), "b": test("integration"), "c": test("unit")}
        self.assertEqual(labels.selection_problems(tests, ["a", "c"], "unit"), [])
        self.assertTrue(labels.selection_problems(tests, ["a", "b", "c"], "unit"))
        self.assertTrue(labels.selection_problems(tests, ["a"], "unit"))
        self.assertTrue(labels.selection_problems(tests, [], "unit"))

    def test_summary_lists_quarantined_tests(self):
        tests = {"a": test("unit"), "b": test("golden", "quarantine", issue="12")}
        text = labels.summary(tests)
        self.assertIn("| `unit` | 1 |", text)
        self.assertIn("**Quarantined tests: 1**", text)
        self.assertIn("`b`: issue #12", text)


if __name__ == "__main__":
    unittest.main()
