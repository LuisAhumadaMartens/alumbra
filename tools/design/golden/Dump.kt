package org.alumbra.designtokens.golden

import org.alumbra.designtokens.Color
import org.alumbra.designtokens.Context
import org.alumbra.designtokens.Contrast
import org.alumbra.designtokens.Density
import org.alumbra.designtokens.DynamicRange
import org.alumbra.designtokens.Motion
import org.alumbra.designtokens.Overrides
import org.alumbra.designtokens.Rgb
import org.alumbra.designtokens.ShadowLayer
import org.alumbra.designtokens.ColorSpace
import org.alumbra.designtokens.Theme
import org.alumbra.designtokens.Tokens
import org.alumbra.designtokens.Value

private fun string(value: String): String {
    val builder = StringBuilder("\"")
    for (character in value) {
        when {
            character == '"' -> builder.append("\\\"")
            character == '\\' -> builder.append("\\\\")
            character < ' ' -> builder.append(String.format("\\u%04x", character.code))
            else -> builder.append(character)
        }
    }
    return builder.append('"').toString()
}

private fun rgb(value: Rgb): String = "[${value.red},${value.green},${value.blue}]"

private fun color(value: Color): String =
    "{\"colorSpace\":${string(value.colorSpace.id)},\"components\":${rgb(value.components)}," +
        "\"alpha\":${value.alpha},\"srgb\":${rgb(value.srgb)}}"

private fun layer(value: ShadowLayer): String =
    "{\"color\":${color(value.color)},\"offsetX\":${value.offsetX},\"offsetY\":${value.offsetY}," +
        "\"blur\":${value.blur},\"spread\":${value.spread},\"inset\":${value.inset}}"

private fun canonical(value: Value): String =
    when (value) {
        is Value.ColorValue -> color(value.value)
        is Value.DimensionValue -> value.value.toString()
        is Value.DurationValue -> value.value.toString()
        is Value.NumberValue -> value.value.toString()
        is Value.FontWeightValue -> value.value.toString()
        is Value.FontFamilyValue -> value.value.joinToString(",", "[", "]") { string(it) }
        is Value.CubicBezierValue ->
            "[${value.value.x1},${value.value.y1},${value.value.x2},${value.value.y2}]"
        is Value.ShadowValue -> value.value.joinToString(",", "[", "]") { layer(it) }
    }

private fun dump(tokens: Tokens): String =
    tokens.entries.joinToString(",", "{", "}") { (name, value) ->
        "${string(name)}:${canonical(value)}"
    }

private fun srgb(red: Double, green: Double, blue: Double, alpha: Double): Color {
    val components = Rgb(red, green, blue)
    return Color(ColorSpace.SRGB, components, alpha, components)
}

fun main() {
    val contexts = mutableListOf<String>()
    for (theme in Theme.entries) {
        for (contrast in Contrast.entries) {
            for (density in Density.entries) {
                for (motion in Motion.entries) {
                    for (dynamicRange in DynamicRange.entries) {
                        val context = Context(theme, contrast, density, motion, dynamicRange)
                        val key =
                            listOf(theme.id, contrast.id, density.id, motion.id, dynamicRange.id)
                                .joinToString("/")
                        contexts.add("${string(key)}:${dump(Tokens(context))}")
                    }
                }
            }
        }
    }
    val semantic =
        Overrides().apply { alumbra_token_color_scontrol_sbackground = srgb(0.25, 0.5, 0.75, 1.0) }
    val component =
        Overrides().apply {
            alumbra_token_color_scontrol_sbackground = srgb(0.25, 0.5, 0.75, 1.0)
            alumbra_token_player_sbutton_sbackground_sfocus = srgb(0.125, 0.25, 0.375, 0.5)
        }
    println(
        "{\"language\":\"kotlin\",\"contexts\":${contexts.joinToString(",", "{", "}")}," +
            "\"scenarios\":{\"semantic\":${dump(Tokens(Context(), semantic))}," +
            "\"component\":${dump(Tokens(Context(), component))}}}"
    )
}
