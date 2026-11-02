import copy
import json
import tempfile
import unittest

import generate


class CompatibilityMatrixTest(unittest.TestCase):
    def setUp(self):
        self.data = generate.load()

    def test_committed_outputs_are_current(self):
        self.assertEqual(generate.main(["--check"]), 0)

    def test_rendering_is_deterministic(self):
        self.assertEqual(generate.render_document(self.data), generate.render_document(self.data))
        self.assertEqual(generate.render_release(self.data), generate.render_release(self.data))

    def test_every_supported_cell_links_to_evidence(self):
        page = generate.render_document(self.data)
        for table in self.data["tables"]:
            url = self.data["evidence"][table["evidence"]]["url"]
            for row in table["rows"]:
                for cell in row[1:]:
                    if generate.SUPPORTED.search(cell):
                        self.assertIn(f"]({url})", page)

    def test_supported_cell_without_row_evidence_fails(self):
        changed = copy.deepcopy(self.data)
        changed["tables"][0]["evidence"] = None
        with self.assertRaisesRegex(generate.MatrixError, "evidence is required"):
            generate.validate(changed)

    def test_unknown_evidence_fails(self):
        changed = copy.deepcopy(self.data)
        changed["tables"][0]["evidence"] = "missing"
        with self.assertRaisesRegex(generate.MatrixError, "unknown evidence"):
            generate.validate(changed)

    def test_tier_rules_cannot_drift_from_adr(self):
        changed = copy.deepcopy(self.data)
        changed["tiers"]["2"] = "Best effort"
        with self.assertRaisesRegex(generate.MatrixError, "match ADR-0039"):
            generate.validate(changed)

    def test_release_input_includes_only_release_tiers(self):
        release = json.loads(generate.render_release(self.data))
        self.assertTrue(all(platform["tier"] in (1, 2) for platform in release["platforms"]))
        self.assertTrue(release["release_tests"])
        self.assertTrue(all(item["evidence"]["url"].startswith("https://")
                            for item in release["release_tests"]))

    def test_column_count_must_match(self):
        changed = copy.deepcopy(self.data)
        changed["tables"][0]["rows"][0].pop()
        with self.assertRaisesRegex(generate.MatrixError, "cell count"):
            generate.validate(changed)


if __name__ == "__main__":
    unittest.main()
