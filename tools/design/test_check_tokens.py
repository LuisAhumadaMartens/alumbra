import unittest

from check_tokens import contrast_ratio, resolve_tokens, tokens_in


class DesignTokenChecksTest(unittest.TestCase):
    def test_root_token_uses_reserved_path_segment(self):
        tokens = list(tokens_in({
            "button": {
                "$type": "color",
                "$root": {
                    "$value": {
                        "colorSpace": "srgb",
                        "components": [0, 0, 0],
                    }
                },
            }
        }))
        self.assertEqual(tokens[0][0], ("button", "$root"))

    def test_alias_cycles_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "circular alias"):
            resolve_tokens({
                "a": {"$type": "number", "$value": "{b}"},
                "b": {"$type": "number", "$value": "{a}"},
            })

    def test_alias_targets_must_exist_and_match_type(self):
        with self.assertRaisesRegex(ValueError, "unresolved alias target"):
            resolve_tokens({"a": {"$type": "number", "$value": "{missing}"}})
        with self.assertRaisesRegex(ValueError, "aliases a color token"):
            resolve_tokens({
                "a": {"$type": "number", "$value": "{b}"},
                "b": {
                    "$type": "color",
                    "$value": {"colorSpace": "srgb", "components": [0, 0, 0]},
                },
            })

    def test_shadow_subvalue_aliases_resolve(self):
        shadow = {
            "color": "{palette.black}",
            "offsetX": "{space.zero}",
            "offsetY": "{space.zero}",
            "blur": "{space.zero}",
            "spread": "{space.zero}",
        }
        resolved = resolve_tokens({
            "palette": {
                "black": {
                    "$type": "color",
                    "$value": {"colorSpace": "srgb", "components": [0, 0, 0]},
                }
            },
            "space": {
                "$type": "dimension",
                "zero": {"$value": {"value": 0, "unit": "px"}},
            },
            "elevation": {"$type": "shadow", "$value": shadow},
        })
        self.assertEqual(resolved[("elevation",)][1]["color"], {
            "colorSpace": "srgb",
            "components": [0, 0, 0],
        })
        self.assertEqual(resolved[("elevation",)][1]["offsetX"], {
            "value": 0,
            "unit": "px",
        })

    def test_wcag_black_white_ratio(self):
        black = {"colorSpace": "srgb", "components": [0, 0, 0]}
        white = {"colorSpace": "srgb", "components": [1, 1, 1]}
        self.assertAlmostEqual(contrast_ratio(black, white), 21)

    def test_translucent_foreground_is_composited_over_opaque_background(self):
        half_black = {
            "colorSpace": "srgb",
            "components": [0, 0, 0],
            "alpha": 0.5,
        }
        white = {"colorSpace": "srgb", "components": [1, 1, 1]}
        self.assertAlmostEqual(contrast_ratio(half_black, white), 3.98, places=2)

    def test_translucent_background_requires_an_opaque_backdrop(self):
        translucent_white = {
            "colorSpace": "srgb",
            "components": [1, 1, 1],
            "alpha": 0.5,
        }
        black = {"colorSpace": "srgb", "components": [0, 0, 0]}
        with self.assertRaisesRegex(ValueError, "declared opaque backdrop"):
            contrast_ratio(black, translucent_white)


if __name__ == "__main__":
    unittest.main()
