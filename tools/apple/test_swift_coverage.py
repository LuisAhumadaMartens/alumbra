import unittest

from swift_coverage import EVENT_TYPES, PLAYER_BINDINGS, report


class SwiftCoverageTest(unittest.TestCase):
    def test_report_lists_each_player_declaration(self):
        output = report()
        for declaration in PLAYER_BINDINGS:
            self.assertIn(f"`Player::{declaration}`", output)

    def test_report_lists_every_cpp_event(self):
        output = report()
        for event in EVENT_TYPES:
            self.assertIn(f"`{event}`", output)

    def test_report_records_settings_and_registration_status(self):
        output = report()
        self.assertIn("alumbra_settings_get", output)
        self.assertIn("Penumbra registration is excluded", output)


if __name__ == "__main__":
    unittest.main()
