import unittest

from check_tokens import (
    BUTTON_ENABLED_STATES,
    check_button_contrast,
    contrast_ratio,
    require_description,
    resolve_tokens,
    tokens_in,
)


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

    def test_tokens_require_non_empty_descriptions(self):
        require_description("Neutral palette shade for surfaces.", "color.neutral.0")
        for description in (None, "", "  "):
            with self.subTest(description=description):
                with self.assertRaisesRegex(ValueError, "non-empty \\$description"):
                    require_description(description, "color.neutral.0")

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

    def test_button_focus_pairs_fail_then_pass_in_both_high_contrast_themes(self):
        black = {"colorSpace": "srgb", "components": [0, 0, 0]}
        white = {"colorSpace": "srgb", "components": [1, 1, 1]}
        yellow = {"colorSpace": "srgb", "components": [1, 1, 0]}

        def check(theme, focus_background, focus_foreground):
            button = {
                layer: {"$type": "color"}
                for layer in ("background", "foreground")
            }
            values = {
                "dark": (black, white),
                "light": (white, black),
            }[theme]
            backgrounds = dict.fromkeys(BUTTON_ENABLED_STATES, values[0])
            foregrounds = dict.fromkeys(BUTTON_ENABLED_STATES, values[1])
            backgrounds["focus"] = focus_background
            foregrounds["focus"] = focus_foreground
            for layer, states in (
                ("background", backgrounds),
                ("foreground", foregrounds),
            ):
                group = button[layer]
                for state, value in states.items():
                    name = "$root" if state == "$root" else state
                    group[name] = {"$value": value}
            resolved = resolve_tokens({"player": {"button": button}})
            return check_button_contrast(
                resolved,
                {"theme": theme, "contrast": "high"},
                {},
            )

        with self.assertRaisesRegex(ValueError, "player.button.focus"):
            check("dark", yellow, white)
        with self.assertRaisesRegex(ValueError, "player.button.focus"):
            check("light", black, black)
        check("dark", black, white)
        check("light", white, black)

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
