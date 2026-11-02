import unittest

import check_wpt_index as check
import generate

INDEX = (check.ROOT / check.INDEX).read_text()
SOURCE = (check.ROOT / check.PORTED).read_text()
TESTS = generate.known_tests(generate.ROOT / generate.TEST_ROOT)


def row(path, status, tests="-", reason="-"):
    return f"| `{path}` | {status} | {tests} | {reason} |"


class WptIndexTest(unittest.TestCase):
    def verify(self, text, ported=frozenset()):
        return check.verify(check.parse(text), TESTS, ported)

    def test_the_committed_index_is_consistent(self):
        rows = check.parse(INDEX)
        errors = check.verify(rows, TESTS, check.ported_tests(SOURCE))
        self.assertEqual(errors, [])
        self.assertGreater(len(rows), 100)

    def test_every_ported_test_is_indexed(self):
        ported = check.ported_tests(SOURCE)
        self.assertGreater(len(ported), 50)
        errors = check.verify(check.parse(INDEX), TESTS,
                              ported | {"WptNothing.Missing"})
        self.assertEqual(errors, ["ported test WptNothing.Missing is not in "
                                  "the index"])

    def test_a_not_applicable_row_needs_a_justification(self):
        self.assertTrue(self.verify(row("a.html", "not-applicable")))
        self.assertTrue(self.verify(row("a.html", "not-applicable", "-",
                                        "too short")))
        self.assertEqual(self.verify(row(
            "a.html", "not-applicable", "-",
            "Needs a DOM that the API does not have.")), [])

    def test_a_deferred_row_needs_a_justification(self):
        self.assertTrue(self.verify(row("a.html", "deferred")))

    def test_an_adapted_row_names_an_existing_test(self):
        self.assertTrue(self.verify(row("a.html", "adapted")))
        self.assertTrue(self.verify(row("a.html", "adapted",
                                        "`WptNothing.Missing`")))
        self.assertEqual(self.verify(row(
            "a.html", "adapted", "`WptNetworkState.InitialValueIsEmpty`")),
            [])

    def test_a_deviation_names_a_disabled_test_and_an_issue(self):
        disabled = ("`WptLoopFromEnded.DISABLED_SettingLoopAfterTheEndClears"
                    "Ended`")
        enabled = "`WptNetworkState.InitialValueIsEmpty`"
        self.assertTrue(self.verify(row("a.html", "deviation", enabled,
                                        "Known deviation #411 here.")))
        self.assertTrue(self.verify(row("a.html", "deviation", disabled,
                                        "Known deviation without a link.")))
        self.assertEqual(self.verify(row("a.html", "deviation", disabled,
                                         "Known deviation #411 here.")), [])

    def test_a_path_is_listed_once(self):
        text = (row("a.html", "adopted", "`WptNetworkState.InitialValueIs"
                    "Empty`") + "\n") * 2
        self.assertEqual(self.verify(text), ["a.html: listed twice"])

    def test_an_unknown_status_is_rejected(self):
        self.assertTrue(self.verify(row("a.html", "ported")))


if __name__ == "__main__":
    unittest.main()
