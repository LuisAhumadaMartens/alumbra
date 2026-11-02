import contextlib
import io
import json
import pathlib
import shutil
import tempfile
import unittest

from check_tokens import (
    BUTTON_ENABLED_STATES,
    check_button_contrast,
    check_contrast,
    contrast_ratio,
    require_description,
    resolve_tokens,
    run,
    tokens_in,
)

REPOSITORY = pathlib.Path(__file__).resolve().parents[2]


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


class TokenTree:
    def __init__(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.directory.name)
        shutil.copytree(REPOSITORY / "design/tokens", self.root / "design/tokens")

    def edit(self, name, change):
        path = self.root / "design/tokens" / name
        data = json.loads(path.read_text(encoding="utf-8"))
        change(data)
        path.write_text(json.dumps(data, indent=2), encoding="utf-8")

    def run(self):
        with contextlib.redirect_stdout(io.StringIO()):
            run(self.root)

    def close(self):
        self.directory.cleanup()


class TokenFailureFixturesTest(unittest.TestCase):
    def tree(self):
        tree = TokenTree()
        self.addCleanup(tree.close)
        return tree

    def assert_fails(self, tree, pattern):
        with self.assertRaisesRegex(ValueError, pattern):
            tree.run()

    def test_unmodified_base_set_passes(self):
        self.tree().run()

    def test_broken_alias_names_the_missing_target(self):
        tree = self.tree()
        tree.edit("semantic.tokens.json", lambda data: data["color"]["accent"]["played"].update(
            {"$value": "{color.blue.999}"}))
        self.assert_fails(tree, r"unresolved alias target color\.blue\.999")

    def test_alias_cycle_names_the_token(self):
        tree = self.tree()

        def change(data):
            surface = data["color"]["surface"]
            surface["base"]["$value"] = "{color.surface.elevated}"
            surface["elevated"]["$value"] = "{color.surface.base}"

        tree.edit("semantic.tokens.json", change)
        self.assert_fails(tree, r"circular alias at color\.surface\.")

    def test_wrong_type_is_reported_against_the_alias_target(self):
        tree = self.tree()
        tree.edit("semantic.tokens.json", lambda data: data["color"]["accent"]["played"].update(
            {"$type": "dimension"}))
        self.assert_fails(
            tree, r"color\.accent\.played aliases a color token, expected dimension")

    def test_unknown_type_is_rejected(self):
        tree = self.tree()
        tree.edit("semantic.tokens.json", lambda data: data["color"]["accent"]["played"].update(
            {"$type": "gradient", "$value": {"colorSpace": "srgb", "components": [0, 0, 0]}}))
        self.assert_fails(tree, r"token type 'gradient' is outside this base-set checker")

    def test_raw_value_in_semantic_tier_is_rejected(self):
        tree = self.tree()
        tree.edit("semantic.tokens.json", lambda data: data["color"]["accent"]["played"].update(
            {"$value": {"colorSpace": "srgb", "components": [0, 0.4, 1]}}))
        self.assert_fails(tree, r"semantic token color\.accent\.played holds a raw value")

    def test_semantic_alias_to_semantic_requires_deprecation(self):
        tree = self.tree()
        tree.edit("semantic.tokens.json", lambda data: data["color"]["accent"]["played"].update(
            {"$value": "{color.border.subtle}"}))
        self.assert_fails(
            tree, r"color\.accent\.played aliases semantic token color\.border\.subtle "
            r"without \$deprecated")

    def test_deprecated_same_tier_alias_is_accepted(self):
        tree = self.tree()
        tree.edit("semantic.tokens.json", lambda data: data["color"]["accent"].update({
            "played_legacy": {
                "$value": "{color.accent.played}",
                "$description": "Deprecated name for color.accent.played.",
                "$deprecated": "Renamed to color.accent.played.",
            }}))
        tree.run()

    def test_deprecated_alias_to_a_component_token_is_rejected(self):
        tree = self.tree()
        tree.edit("semantic.tokens.json", lambda data: data["color"]["accent"].update({
            "played_legacy": {
                "$value": "{player.button.background.$root}",
                "$description": "Deprecated name.",
                "$deprecated": True,
            }}))
        self.assert_fails(tree, r"color\.accent\.played_legacy must reference a primitive token")

    def test_component_may_not_reference_a_primitive(self):
        tree = self.tree()
        tree.edit("components.tokens.json", lambda data: data["player"]["button"]["background"][
            "$root"].update({"$value": "{color.blue.500}"}))
        self.assert_fails(
            tree, r"component token player\.button\.background\.\$root references primitive "
            r"token color\.blue\.500")

    def test_low_contrast_pair_reports_ratio_and_minimum(self):
        tree = self.tree()
        tree.edit("semantic.tokens.json", lambda data: data["color"]["text"]["primary"].update(
            {"$value": "{color.scheme.normal.surface}"}))
        self.assert_fails(
            tree, r"color\.text\.primary on color\.surface\.base is 1\.000:1, below the "
            r"4\.5:1 minimum")

    def test_missing_description_is_rejected(self):
        tree = self.tree()
        tree.edit("semantic.tokens.json", lambda data: data["color"]["accent"]["played"].pop(
            "$description"))
        self.assert_fails(tree, r"color\.accent\.played: token requires a non-empty \$description")

    def test_name_outside_the_lower_snake_rule_is_rejected(self):
        tree = self.tree()
        tree.edit("semantic.tokens.json", lambda data: data["color"]["accent"].update(
            {"Played": data["color"]["accent"]["played"]}))
        self.assert_fails(tree, r"color\.accent\.Played: invalid lower_snake path segment")

    def test_tier_metadata_must_match_the_source_file(self):
        tree = self.tree()
        tree.edit("semantic.tokens.json", lambda data: data["color"]["surface"]["base"][
            "$extensions"]["com.alumbra"].update({"tier": "component"}))
        self.assert_fails(tree, r"declares tier 'component' but lives in the semantic tier file")


class ScrimContrastTest(unittest.TestCase):
    def resolved(self, scrim_alpha):
        def color(components, alpha=None):
            value = {"colorSpace": "srgb", "components": components}
            if alpha is not None:
                value["alpha"] = alpha
            return value

        meta = {"contrast": {"against": ["scrim"], "minimum": 4.5, "backdrop": "frame"}}
        return resolve_tokens({
            "$type": "color",
            "text": {"$value": color([1, 1, 1]), "$extensions": {"com.alumbra": meta}},
            "scrim": {"$value": color([0, 0, 0], scrim_alpha)},
            "frame": {"$value": color([1, 1, 1])},
        })

    def test_scrim_is_composited_over_its_declared_backdrop(self):
        pairs = {("text",): ({"scrim"}, 4.5)}
        measured = {}
        check_contrast(self.resolved(0.72), {"theme": "dark", "contrast": "normal"}, measured, pairs)
        self.assertAlmostEqual(next(iter(measured.values())), 9.23, places=2)

    def test_scrim_too_translucent_for_its_backdrop_fails(self):
        pairs = {("text",): ({"scrim"}, 4.5)}
        with self.assertRaisesRegex(ValueError, r"text on scrim is 1\.\d+:1, below the 4\.5:1"):
            check_contrast(
                self.resolved(0.1), {"theme": "dark", "contrast": "high"}, {}, pairs)


if __name__ == "__main__":
    unittest.main()
