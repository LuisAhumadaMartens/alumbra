package org.alumbra.designtokens

enum class Theme(val id: String) {
    DARK("dark"),
    LIGHT("light"),
}

enum class Contrast(val id: String) {
    NORMAL("normal"),
    HIGH("high"),
}

enum class Density(val id: String) {
    PHONE("phone"),
    TABLET("tablet"),
    DESKTOP("desktop"),
    TV("tv"),
}

enum class Motion(val id: String) {
    FULL("full"),
    REDUCED("reduced"),
}

enum class DynamicRange(val id: String) {
    SDR("sdr"),
    HDR("hdr"),
}

data class Context(
    val theme: Theme = Theme.DARK,
    val contrast: Contrast = Contrast.NORMAL,
    val density: Density = Density.DESKTOP,
    val motion: Motion = Motion.FULL,
    val dynamicRange: DynamicRange = DynamicRange.SDR,
)

enum class ColorSpace(val id: String) {
    SRGB("srgb"),
    DISPLAY_P3("display-p3"),
    REC2020("rec2020"),
}

data class Rgb(val red: Double, val green: Double, val blue: Double)

data class Color(
    val colorSpace: ColorSpace,
    val components: Rgb,
    val alpha: Double,
    val srgb: Rgb,
)

data class CubicBezier(val x1: Double, val y1: Double, val x2: Double, val y2: Double)

data class ShadowLayer(
    val color: Color,
    val offsetX: Double,
    val offsetY: Double,
    val blur: Double,
    val spread: Double,
    val inset: Boolean,
)

sealed interface Value {
    data class ColorValue(val value: Color) : Value

    data class DimensionValue(val value: Double) : Value

    data class DurationValue(val value: Double) : Value

    data class NumberValue(val value: Double) : Value

    data class FontWeightValue(val value: Double) : Value

    data class FontFamilyValue(val value: List<String>) : Value

    data class CubicBezierValue(val value: CubicBezier) : Value

    data class ShadowValue(val value: List<ShadowLayer>) : Value
}

class Overrides {
    var alumbra_token_color_saccent_splayed: Color? = null
    var alumbra_token_color_sborder_ssubtle: Color? = null
    var alumbra_token_color_scontrol_sbackground: Color? = null
    var alumbra_token_color_scontrol_sdisabled: Color? = null
    var alumbra_token_color_scontrol_sforeground: Color? = null
    var alumbra_token_color_scontrol_shover: Color? = null
    var alumbra_token_color_scontrol_spressed: Color? = null
    var alumbra_token_color_sfocus_sring: Color? = null
    var alumbra_token_color_ssurface_sbase: Color? = null
    var alumbra_token_color_ssurface_selevated: Color? = null
    var alumbra_token_color_ssurface_soverlay: Color? = null
    var alumbra_token_color_ssurface_sscrim: Color? = null
    var alumbra_token_color_stext_sdisabled: Color? = null
    var alumbra_token_color_stext_sprimary: Color? = null
    var alumbra_token_color_stext_ssecondary: Color? = null
    var alumbra_token_elevation_smodal: List<ShadowLayer>? = null
    var alumbra_token_elevation_spanel: List<ShadowLayer>? = null
    var alumbra_token_elevation_sraised: List<ShadowLayer>? = null
    var alumbra_token_layer_sad_uchrome: Double? = null
    var alumbra_token_layer_sad_usdk_ucontainer: Double? = null
    var alumbra_token_layer_scaptions: Double? = null
    var alumbra_token_layer_scontrols: Double? = null
    var alumbra_token_layer_svideo_usurface: Double? = null
    var alumbra_token_luminance_shdr_uhighlight_uceiling: Double? = null
    var alumbra_token_luminance_sreference_uwhite: Double? = null
    var alumbra_token_motion_sduration_sinstant: Double? = null
    var alumbra_token_motion_sduration_slong: Double? = null
    var alumbra_token_motion_sduration_smedium: Double? = null
    var alumbra_token_motion_sduration_sshort: Double? = null
    var alumbra_token_motion_seasing_semphasized: CubicBezier? = null
    var alumbra_token_motion_seasing_sstandard: CubicBezier? = null
    var alumbra_token_opacity_sdisabled: Double? = null
    var alumbra_token_radius_slarge: Double? = null
    var alumbra_token_radius_smedium: Double? = null
    var alumbra_token_radius_spill: Double? = null
    var alumbra_token_radius_ssmall: Double? = null
    var alumbra_token_spacing_sextra_ularge: Double? = null
    var alumbra_token_spacing_slarge: Double? = null
    var alumbra_token_spacing_smedium: Double? = null
    var alumbra_token_spacing_snone: Double? = null
    var alumbra_token_spacing_ssmall: Double? = null
    var alumbra_token_spacing_stight: Double? = null
    var alumbra_token_target_sminimum: Double? = null
    var alumbra_token_typography_sfont_ufamily_sui: List<String>? = null
    var alumbra_token_typography_sfont_usize_sbody: Double? = null
    var alumbra_token_typography_sfont_usize_scaption: Double? = null
    var alumbra_token_typography_sfont_usize_sdisplay: Double? = null
    var alumbra_token_typography_sfont_usize_stitle: Double? = null
    var alumbra_token_typography_sfont_uweight_sbold: Double? = null
    var alumbra_token_typography_sfont_uweight_smedium: Double? = null
    var alumbra_token_typography_sfont_uweight_sregular: Double? = null
    var alumbra_token_typography_sline_uheight_sbody: Double? = null
    var alumbra_token_typography_sline_uheight_scompact: Double? = null
    var alumbra_token_player_sad_sband_sbackground: Color? = null
    var alumbra_token_player_sad_uband_sbackground: Color? = null
    var alumbra_token_player_sad_uchrome_sbadge_sbackground: Color? = null
    var alumbra_token_player_sbutton_sbackground: Color? = null
    var alumbra_token_player_sbutton_sbackground_sdisabled: Color? = null
    var alumbra_token_player_sbutton_sbackground_sfocus: Color? = null
    var alumbra_token_player_sbutton_sbackground_shover: Color? = null
    var alumbra_token_player_sbutton_sbackground_spressed: Color? = null
    var alumbra_token_player_sbutton_sforeground: Color? = null
    var alumbra_token_player_sbutton_sforeground_sdisabled: Color? = null
    var alumbra_token_player_sbutton_sforeground_sfocus: Color? = null
    var alumbra_token_player_sbutton_sforeground_shover: Color? = null
    var alumbra_token_player_sbutton_sforeground_spressed: Color? = null
    var alumbra_token_player_sbutton_sradius: Double? = null
    var alumbra_token_player_scaptions_sfont_usize: Double? = null
    var alumbra_token_player_scaptions_stext: Color? = null
    var alumbra_token_player_scaptions_swindow: Color? = null
    var alumbra_token_player_sfocus_sring: Color? = null
    var alumbra_token_player_sseekbar_splayed: Color? = null
    var alumbra_token_player_sseekbar_strack: Color? = null
}

class Tokens(context: Context = Context(), overrides: Overrides = Overrides()) {
    val alumbra_token_color_saccent_splayed: Color =
        overrides.alumbra_token_color_saccent_splayed
            ?: select(Defaults.alumbra_token_color_saccent_splayed, context)
    val alumbra_token_color_sborder_ssubtle: Color =
        overrides.alumbra_token_color_sborder_ssubtle
            ?: select(Defaults.alumbra_token_color_sborder_ssubtle, context, Modifier.THEME, Modifier.CONTRAST)
    val alumbra_token_color_scontrol_sbackground: Color =
        overrides.alumbra_token_color_scontrol_sbackground
            ?: select(Defaults.alumbra_token_color_scontrol_sbackground, context, Modifier.THEME, Modifier.CONTRAST)
    val alumbra_token_color_scontrol_sdisabled: Color =
        overrides.alumbra_token_color_scontrol_sdisabled
            ?: select(Defaults.alumbra_token_color_scontrol_sdisabled, context, Modifier.THEME, Modifier.CONTRAST)
    val alumbra_token_color_scontrol_sforeground: Color =
        overrides.alumbra_token_color_scontrol_sforeground
            ?: select(Defaults.alumbra_token_color_scontrol_sforeground, context, Modifier.THEME, Modifier.CONTRAST)
    val alumbra_token_color_scontrol_shover: Color =
        overrides.alumbra_token_color_scontrol_shover
            ?: select(Defaults.alumbra_token_color_scontrol_shover, context, Modifier.THEME, Modifier.CONTRAST)
    val alumbra_token_color_scontrol_spressed: Color =
        overrides.alumbra_token_color_scontrol_spressed
            ?: select(Defaults.alumbra_token_color_scontrol_spressed, context, Modifier.THEME, Modifier.CONTRAST)
    val alumbra_token_color_sfocus_sring: Color =
        overrides.alumbra_token_color_sfocus_sring
            ?: select(Defaults.alumbra_token_color_sfocus_sring, context, Modifier.THEME, Modifier.CONTRAST)
    val alumbra_token_color_ssurface_sbase: Color =
        overrides.alumbra_token_color_ssurface_sbase
            ?: select(Defaults.alumbra_token_color_ssurface_sbase, context, Modifier.THEME, Modifier.CONTRAST)
    val alumbra_token_color_ssurface_selevated: Color =
        overrides.alumbra_token_color_ssurface_selevated
            ?: select(Defaults.alumbra_token_color_ssurface_selevated, context, Modifier.THEME, Modifier.CONTRAST)
    val alumbra_token_color_ssurface_soverlay: Color =
        overrides.alumbra_token_color_ssurface_soverlay
            ?: select(Defaults.alumbra_token_color_ssurface_soverlay, context, Modifier.THEME, Modifier.CONTRAST)
    val alumbra_token_color_ssurface_sscrim: Color =
        overrides.alumbra_token_color_ssurface_sscrim
            ?: select(Defaults.alumbra_token_color_ssurface_sscrim, context)
    val alumbra_token_color_stext_sdisabled: Color =
        overrides.alumbra_token_color_stext_sdisabled
            ?: select(Defaults.alumbra_token_color_stext_sdisabled, context)
    val alumbra_token_color_stext_sprimary: Color =
        overrides.alumbra_token_color_stext_sprimary
            ?: select(Defaults.alumbra_token_color_stext_sprimary, context, Modifier.THEME, Modifier.CONTRAST)
    val alumbra_token_color_stext_ssecondary: Color =
        overrides.alumbra_token_color_stext_ssecondary
            ?: select(Defaults.alumbra_token_color_stext_ssecondary, context, Modifier.THEME, Modifier.CONTRAST)
    val alumbra_token_elevation_smodal: List<ShadowLayer> =
        overrides.alumbra_token_elevation_smodal
            ?: select(Defaults.alumbra_token_elevation_smodal, context)
    val alumbra_token_elevation_spanel: List<ShadowLayer> =
        overrides.alumbra_token_elevation_spanel
            ?: select(Defaults.alumbra_token_elevation_spanel, context)
    val alumbra_token_elevation_sraised: List<ShadowLayer> =
        overrides.alumbra_token_elevation_sraised
            ?: select(Defaults.alumbra_token_elevation_sraised, context)
    val alumbra_token_layer_sad_uchrome: Double =
        overrides.alumbra_token_layer_sad_uchrome
            ?: select(Defaults.alumbra_token_layer_sad_uchrome, context)
    val alumbra_token_layer_sad_usdk_ucontainer: Double =
        overrides.alumbra_token_layer_sad_usdk_ucontainer
            ?: select(Defaults.alumbra_token_layer_sad_usdk_ucontainer, context)
    val alumbra_token_layer_scaptions: Double =
        overrides.alumbra_token_layer_scaptions
            ?: select(Defaults.alumbra_token_layer_scaptions, context)
    val alumbra_token_layer_scontrols: Double =
        overrides.alumbra_token_layer_scontrols
            ?: select(Defaults.alumbra_token_layer_scontrols, context)
    val alumbra_token_layer_svideo_usurface: Double =
        overrides.alumbra_token_layer_svideo_usurface
            ?: select(Defaults.alumbra_token_layer_svideo_usurface, context)
    val alumbra_token_luminance_shdr_uhighlight_uceiling: Double =
        overrides.alumbra_token_luminance_shdr_uhighlight_uceiling
            ?: select(Defaults.alumbra_token_luminance_shdr_uhighlight_uceiling, context, Modifier.DYNAMIC_RANGE)
    val alumbra_token_luminance_sreference_uwhite: Double =
        overrides.alumbra_token_luminance_sreference_uwhite
            ?: select(Defaults.alumbra_token_luminance_sreference_uwhite, context)
    val alumbra_token_motion_sduration_sinstant: Double =
        overrides.alumbra_token_motion_sduration_sinstant
            ?: select(Defaults.alumbra_token_motion_sduration_sinstant, context)
    val alumbra_token_motion_sduration_slong: Double =
        overrides.alumbra_token_motion_sduration_slong
            ?: select(Defaults.alumbra_token_motion_sduration_slong, context, Modifier.MOTION)
    val alumbra_token_motion_sduration_smedium: Double =
        overrides.alumbra_token_motion_sduration_smedium
            ?: select(Defaults.alumbra_token_motion_sduration_smedium, context, Modifier.MOTION)
    val alumbra_token_motion_sduration_sshort: Double =
        overrides.alumbra_token_motion_sduration_sshort
            ?: select(Defaults.alumbra_token_motion_sduration_sshort, context, Modifier.MOTION)
    val alumbra_token_motion_seasing_semphasized: CubicBezier =
        overrides.alumbra_token_motion_seasing_semphasized
            ?: select(Defaults.alumbra_token_motion_seasing_semphasized, context)
    val alumbra_token_motion_seasing_sstandard: CubicBezier =
        overrides.alumbra_token_motion_seasing_sstandard
            ?: select(Defaults.alumbra_token_motion_seasing_sstandard, context)
    val alumbra_token_opacity_sdisabled: Double =
        overrides.alumbra_token_opacity_sdisabled
            ?: select(Defaults.alumbra_token_opacity_sdisabled, context)
    val alumbra_token_radius_slarge: Double =
        overrides.alumbra_token_radius_slarge
            ?: select(Defaults.alumbra_token_radius_slarge, context)
    val alumbra_token_radius_smedium: Double =
        overrides.alumbra_token_radius_smedium
            ?: select(Defaults.alumbra_token_radius_smedium, context)
    val alumbra_token_radius_spill: Double =
        overrides.alumbra_token_radius_spill
            ?: select(Defaults.alumbra_token_radius_spill, context)
    val alumbra_token_radius_ssmall: Double =
        overrides.alumbra_token_radius_ssmall
            ?: select(Defaults.alumbra_token_radius_ssmall, context)
    val alumbra_token_spacing_sextra_ularge: Double =
        overrides.alumbra_token_spacing_sextra_ularge
            ?: select(Defaults.alumbra_token_spacing_sextra_ularge, context, Modifier.DENSITY)
    val alumbra_token_spacing_slarge: Double =
        overrides.alumbra_token_spacing_slarge
            ?: select(Defaults.alumbra_token_spacing_slarge, context, Modifier.DENSITY)
    val alumbra_token_spacing_smedium: Double =
        overrides.alumbra_token_spacing_smedium
            ?: select(Defaults.alumbra_token_spacing_smedium, context, Modifier.DENSITY)
    val alumbra_token_spacing_snone: Double =
        overrides.alumbra_token_spacing_snone
            ?: select(Defaults.alumbra_token_spacing_snone, context)
    val alumbra_token_spacing_ssmall: Double =
        overrides.alumbra_token_spacing_ssmall
            ?: select(Defaults.alumbra_token_spacing_ssmall, context, Modifier.DENSITY)
    val alumbra_token_spacing_stight: Double =
        overrides.alumbra_token_spacing_stight
            ?: select(Defaults.alumbra_token_spacing_stight, context, Modifier.DENSITY)
    val alumbra_token_target_sminimum: Double =
        overrides.alumbra_token_target_sminimum
            ?: select(Defaults.alumbra_token_target_sminimum, context, Modifier.DENSITY)
    val alumbra_token_typography_sfont_ufamily_sui: List<String> =
        overrides.alumbra_token_typography_sfont_ufamily_sui
            ?: select(Defaults.alumbra_token_typography_sfont_ufamily_sui, context)
    val alumbra_token_typography_sfont_usize_sbody: Double =
        overrides.alumbra_token_typography_sfont_usize_sbody
            ?: select(Defaults.alumbra_token_typography_sfont_usize_sbody, context, Modifier.DENSITY)
    val alumbra_token_typography_sfont_usize_scaption: Double =
        overrides.alumbra_token_typography_sfont_usize_scaption
            ?: select(Defaults.alumbra_token_typography_sfont_usize_scaption, context, Modifier.DENSITY)
    val alumbra_token_typography_sfont_usize_sdisplay: Double =
        overrides.alumbra_token_typography_sfont_usize_sdisplay
            ?: select(Defaults.alumbra_token_typography_sfont_usize_sdisplay, context, Modifier.DENSITY)
    val alumbra_token_typography_sfont_usize_stitle: Double =
        overrides.alumbra_token_typography_sfont_usize_stitle
            ?: select(Defaults.alumbra_token_typography_sfont_usize_stitle, context, Modifier.DENSITY)
    val alumbra_token_typography_sfont_uweight_sbold: Double =
        overrides.alumbra_token_typography_sfont_uweight_sbold
            ?: select(Defaults.alumbra_token_typography_sfont_uweight_sbold, context)
    val alumbra_token_typography_sfont_uweight_smedium: Double =
        overrides.alumbra_token_typography_sfont_uweight_smedium
            ?: select(Defaults.alumbra_token_typography_sfont_uweight_smedium, context)
    val alumbra_token_typography_sfont_uweight_sregular: Double =
        overrides.alumbra_token_typography_sfont_uweight_sregular
            ?: select(Defaults.alumbra_token_typography_sfont_uweight_sregular, context)
    val alumbra_token_typography_sline_uheight_sbody: Double =
        overrides.alumbra_token_typography_sline_uheight_sbody
            ?: select(Defaults.alumbra_token_typography_sline_uheight_sbody, context)
    val alumbra_token_typography_sline_uheight_scompact: Double =
        overrides.alumbra_token_typography_sline_uheight_scompact
            ?: select(Defaults.alumbra_token_typography_sline_uheight_scompact, context)
    val alumbra_token_player_sad_sband_sbackground: Color =
        overrides.alumbra_token_player_sad_sband_sbackground
            ?: alumbra_token_color_ssurface_selevated
    val alumbra_token_player_sad_uband_sbackground: Color =
        overrides.alumbra_token_player_sad_uband_sbackground
            ?: alumbra_token_color_ssurface_soverlay
    val alumbra_token_player_sad_uchrome_sbadge_sbackground: Color =
        overrides.alumbra_token_player_sad_uchrome_sbadge_sbackground
            ?: alumbra_token_color_scontrol_sbackground
    val alumbra_token_player_sbutton_sbackground: Color =
        overrides.alumbra_token_player_sbutton_sbackground
            ?: alumbra_token_color_scontrol_sbackground
    val alumbra_token_player_sbutton_sbackground_sdisabled: Color =
        overrides.alumbra_token_player_sbutton_sbackground_sdisabled
            ?: alumbra_token_color_scontrol_sdisabled
    val alumbra_token_player_sbutton_sbackground_sfocus: Color =
        overrides.alumbra_token_player_sbutton_sbackground_sfocus
            ?: alumbra_token_color_scontrol_sbackground
    val alumbra_token_player_sbutton_sbackground_shover: Color =
        overrides.alumbra_token_player_sbutton_sbackground_shover
            ?: alumbra_token_color_scontrol_shover
    val alumbra_token_player_sbutton_sbackground_spressed: Color =
        overrides.alumbra_token_player_sbutton_sbackground_spressed
            ?: alumbra_token_color_scontrol_spressed
    val alumbra_token_player_sbutton_sforeground: Color =
        overrides.alumbra_token_player_sbutton_sforeground
            ?: alumbra_token_color_scontrol_sforeground
    val alumbra_token_player_sbutton_sforeground_sdisabled: Color =
        overrides.alumbra_token_player_sbutton_sforeground_sdisabled
            ?: alumbra_token_color_stext_sdisabled
    val alumbra_token_player_sbutton_sforeground_sfocus: Color =
        overrides.alumbra_token_player_sbutton_sforeground_sfocus
            ?: alumbra_token_color_scontrol_sforeground
    val alumbra_token_player_sbutton_sforeground_shover: Color =
        overrides.alumbra_token_player_sbutton_sforeground_shover
            ?: alumbra_token_color_scontrol_sforeground
    val alumbra_token_player_sbutton_sforeground_spressed: Color =
        overrides.alumbra_token_player_sbutton_sforeground_spressed
            ?: alumbra_token_color_scontrol_sforeground
    val alumbra_token_player_sbutton_sradius: Double =
        overrides.alumbra_token_player_sbutton_sradius
            ?: alumbra_token_radius_smedium
    val alumbra_token_player_scaptions_sfont_usize: Double =
        overrides.alumbra_token_player_scaptions_sfont_usize
            ?: alumbra_token_typography_sfont_usize_scaption
    val alumbra_token_player_scaptions_stext: Color =
        overrides.alumbra_token_player_scaptions_stext
            ?: alumbra_token_color_stext_sprimary
    val alumbra_token_player_scaptions_swindow: Color =
        overrides.alumbra_token_player_scaptions_swindow
            ?: alumbra_token_color_ssurface_sscrim
    val alumbra_token_player_sfocus_sring: Color =
        overrides.alumbra_token_player_sfocus_sring
            ?: alumbra_token_color_sfocus_sring
    val alumbra_token_player_sseekbar_splayed: Color =
        overrides.alumbra_token_player_sseekbar_splayed
            ?: alumbra_token_color_saccent_splayed
    val alumbra_token_player_sseekbar_strack: Color =
        overrides.alumbra_token_player_sseekbar_strack
            ?: alumbra_token_color_sborder_ssubtle

    val entries: List<Pair<String, Value>>
        get() = listOf(
            "color.accent.played" to Value.ColorValue(alumbra_token_color_saccent_splayed),
            "color.border.subtle" to Value.ColorValue(alumbra_token_color_sborder_ssubtle),
            "color.control.background" to Value.ColorValue(alumbra_token_color_scontrol_sbackground),
            "color.control.disabled" to Value.ColorValue(alumbra_token_color_scontrol_sdisabled),
            "color.control.foreground" to Value.ColorValue(alumbra_token_color_scontrol_sforeground),
            "color.control.hover" to Value.ColorValue(alumbra_token_color_scontrol_shover),
            "color.control.pressed" to Value.ColorValue(alumbra_token_color_scontrol_spressed),
            "color.focus.ring" to Value.ColorValue(alumbra_token_color_sfocus_sring),
            "color.surface.base" to Value.ColorValue(alumbra_token_color_ssurface_sbase),
            "color.surface.elevated" to Value.ColorValue(alumbra_token_color_ssurface_selevated),
            "color.surface.overlay" to Value.ColorValue(alumbra_token_color_ssurface_soverlay),
            "color.surface.scrim" to Value.ColorValue(alumbra_token_color_ssurface_sscrim),
            "color.text.disabled" to Value.ColorValue(alumbra_token_color_stext_sdisabled),
            "color.text.primary" to Value.ColorValue(alumbra_token_color_stext_sprimary),
            "color.text.secondary" to Value.ColorValue(alumbra_token_color_stext_ssecondary),
            "elevation.modal" to Value.ShadowValue(alumbra_token_elevation_smodal),
            "elevation.panel" to Value.ShadowValue(alumbra_token_elevation_spanel),
            "elevation.raised" to Value.ShadowValue(alumbra_token_elevation_sraised),
            "layer.ad_chrome" to Value.NumberValue(alumbra_token_layer_sad_uchrome),
            "layer.ad_sdk_container" to Value.NumberValue(alumbra_token_layer_sad_usdk_ucontainer),
            "layer.captions" to Value.NumberValue(alumbra_token_layer_scaptions),
            "layer.controls" to Value.NumberValue(alumbra_token_layer_scontrols),
            "layer.video_surface" to Value.NumberValue(alumbra_token_layer_svideo_usurface),
            "luminance.hdr_highlight_ceiling" to Value.NumberValue(alumbra_token_luminance_shdr_uhighlight_uceiling),
            "luminance.reference_white" to Value.NumberValue(alumbra_token_luminance_sreference_uwhite),
            "motion.duration.instant" to Value.DurationValue(alumbra_token_motion_sduration_sinstant),
            "motion.duration.long" to Value.DurationValue(alumbra_token_motion_sduration_slong),
            "motion.duration.medium" to Value.DurationValue(alumbra_token_motion_sduration_smedium),
            "motion.duration.short" to Value.DurationValue(alumbra_token_motion_sduration_sshort),
            "motion.easing.emphasized" to Value.CubicBezierValue(alumbra_token_motion_seasing_semphasized),
            "motion.easing.standard" to Value.CubicBezierValue(alumbra_token_motion_seasing_sstandard),
            "opacity.disabled" to Value.NumberValue(alumbra_token_opacity_sdisabled),
            "radius.large" to Value.DimensionValue(alumbra_token_radius_slarge),
            "radius.medium" to Value.DimensionValue(alumbra_token_radius_smedium),
            "radius.pill" to Value.DimensionValue(alumbra_token_radius_spill),
            "radius.small" to Value.DimensionValue(alumbra_token_radius_ssmall),
            "spacing.extra_large" to Value.DimensionValue(alumbra_token_spacing_sextra_ularge),
            "spacing.large" to Value.DimensionValue(alumbra_token_spacing_slarge),
            "spacing.medium" to Value.DimensionValue(alumbra_token_spacing_smedium),
            "spacing.none" to Value.DimensionValue(alumbra_token_spacing_snone),
            "spacing.small" to Value.DimensionValue(alumbra_token_spacing_ssmall),
            "spacing.tight" to Value.DimensionValue(alumbra_token_spacing_stight),
            "target.minimum" to Value.DimensionValue(alumbra_token_target_sminimum),
            "typography.font_family.ui" to Value.FontFamilyValue(alumbra_token_typography_sfont_ufamily_sui),
            "typography.font_size.body" to Value.DimensionValue(alumbra_token_typography_sfont_usize_sbody),
            "typography.font_size.caption" to Value.DimensionValue(alumbra_token_typography_sfont_usize_scaption),
            "typography.font_size.display" to Value.DimensionValue(alumbra_token_typography_sfont_usize_sdisplay),
            "typography.font_size.title" to Value.DimensionValue(alumbra_token_typography_sfont_usize_stitle),
            "typography.font_weight.bold" to Value.FontWeightValue(alumbra_token_typography_sfont_uweight_sbold),
            "typography.font_weight.medium" to Value.FontWeightValue(alumbra_token_typography_sfont_uweight_smedium),
            "typography.font_weight.regular" to Value.FontWeightValue(alumbra_token_typography_sfont_uweight_sregular),
            "typography.line_height.body" to Value.NumberValue(alumbra_token_typography_sline_uheight_sbody),
            "typography.line_height.compact" to Value.NumberValue(alumbra_token_typography_sline_uheight_scompact),
            "player.ad.band.background" to Value.ColorValue(alumbra_token_player_sad_sband_sbackground),
            "player.ad_band.background" to Value.ColorValue(alumbra_token_player_sad_uband_sbackground),
            "player.ad_chrome.badge.background" to Value.ColorValue(alumbra_token_player_sad_uchrome_sbadge_sbackground),
            "player.button.background" to Value.ColorValue(alumbra_token_player_sbutton_sbackground),
            "player.button.background.disabled" to Value.ColorValue(alumbra_token_player_sbutton_sbackground_sdisabled),
            "player.button.background.focus" to Value.ColorValue(alumbra_token_player_sbutton_sbackground_sfocus),
            "player.button.background.hover" to Value.ColorValue(alumbra_token_player_sbutton_sbackground_shover),
            "player.button.background.pressed" to Value.ColorValue(alumbra_token_player_sbutton_sbackground_spressed),
            "player.button.foreground" to Value.ColorValue(alumbra_token_player_sbutton_sforeground),
            "player.button.foreground.disabled" to Value.ColorValue(alumbra_token_player_sbutton_sforeground_sdisabled),
            "player.button.foreground.focus" to Value.ColorValue(alumbra_token_player_sbutton_sforeground_sfocus),
            "player.button.foreground.hover" to Value.ColorValue(alumbra_token_player_sbutton_sforeground_shover),
            "player.button.foreground.pressed" to Value.ColorValue(alumbra_token_player_sbutton_sforeground_spressed),
            "player.button.radius" to Value.DimensionValue(alumbra_token_player_sbutton_sradius),
            "player.captions.font_size" to Value.DimensionValue(alumbra_token_player_scaptions_sfont_usize),
            "player.captions.text" to Value.ColorValue(alumbra_token_player_scaptions_stext),
            "player.captions.window" to Value.ColorValue(alumbra_token_player_scaptions_swindow),
            "player.focus.ring" to Value.ColorValue(alumbra_token_player_sfocus_sring),
            "player.seekbar.played" to Value.ColorValue(alumbra_token_player_sseekbar_splayed),
            "player.seekbar.track" to Value.ColorValue(alumbra_token_player_sseekbar_strack),
        )
}

private enum class Modifier {
    THEME,
    CONTRAST,
    DENSITY,
    MOTION,
    DYNAMIC_RANGE,
}

private fun <T> select(values: List<T>, context: Context, vararg modifiers: Modifier): T {
    var index = 0
    for (modifier in modifiers) {
        index = when (modifier) {
            Modifier.THEME -> index * Theme.entries.size + context.theme.ordinal
            Modifier.CONTRAST -> index * Contrast.entries.size + context.contrast.ordinal
            Modifier.DENSITY -> index * Density.entries.size + context.density.ordinal
            Modifier.MOTION -> index * Motion.entries.size + context.motion.ordinal
            Modifier.DYNAMIC_RANGE ->
                index * DynamicRange.entries.size + context.dynamicRange.ordinal
        }
    }
    return values[index]
}

private fun srgb(red: Double, green: Double, blue: Double, alpha: Double = 1.0): Color {
    val components = Rgb(red, green, blue)
    return Color(ColorSpace.SRGB, components, alpha, components)
}

private fun bezier(x1: Double, y1: Double, x2: Double, y2: Double): CubicBezier =
    CubicBezier(x1, y1, x2, y2)

private fun layer(
    color: Color,
    offsetX: Double,
    offsetY: Double,
    blur: Double,
    spread: Double,
    inset: Boolean,
): ShadowLayer = ShadowLayer(color, offsetX, offsetY, blur, spread, inset)

private object Defaults {
    val alumbra_token_color_saccent_splayed: List<Color> = listOf(
      srgb(0.055, 0.647, 0.914),
    )
    val alumbra_token_color_sborder_ssubtle: List<Color> = listOf(
      srgb(0.392, 0.455, 0.545),
      srgb(1.0, 1.0, 1.0),
      srgb(0.278, 0.333, 0.412),
      srgb(0.0, 0.0, 0.0),
    )
    val alumbra_token_color_scontrol_sbackground: List<Color> = listOf(
      srgb(0.118, 0.161, 0.231),
      srgb(0.0, 0.0, 0.0),
      srgb(0.886, 0.91, 0.941),
      srgb(1.0, 1.0, 1.0),
    )
    val alumbra_token_color_scontrol_sdisabled: List<Color> = listOf(
      srgb(0.118, 0.161, 0.231),
      srgb(0.0, 0.0, 0.0),
      srgb(0.945, 0.961, 0.976),
      srgb(1.0, 1.0, 1.0),
    )
    val alumbra_token_color_scontrol_sforeground: List<Color> = listOf(
      srgb(1.0, 1.0, 1.0),
      srgb(1.0, 1.0, 1.0),
      srgb(0.059, 0.09, 0.165),
      srgb(0.0, 0.0, 0.0),
    )
    val alumbra_token_color_scontrol_shover: List<Color> = listOf(
      srgb(0.2, 0.255, 0.333),
      srgb(0.059, 0.09, 0.165),
      srgb(0.796, 0.835, 0.882),
      srgb(0.886, 0.91, 0.941),
    )
    val alumbra_token_color_scontrol_spressed: List<Color> = listOf(
      srgb(0.278, 0.333, 0.412),
      srgb(0.118, 0.161, 0.231),
      srgb(0.58, 0.639, 0.722),
      srgb(0.796, 0.835, 0.882),
    )
    val alumbra_token_color_sfocus_sring: List<Color> = listOf(
      srgb(0.055, 0.647, 0.914),
      srgb(1.0, 1.0, 0.0),
      srgb(0.012, 0.412, 0.631),
      srgb(0.0, 0.0, 0.0),
    )
    val alumbra_token_color_ssurface_sbase: List<Color> = listOf(
      srgb(0.035, 0.055, 0.094),
      srgb(0.0, 0.0, 0.0),
      srgb(1.0, 1.0, 1.0),
      srgb(1.0, 1.0, 1.0),
    )
    val alumbra_token_color_ssurface_selevated: List<Color> = listOf(
      srgb(0.059, 0.09, 0.165),
      srgb(0.0, 0.0, 0.0),
      srgb(0.973, 0.98, 0.988),
      srgb(1.0, 1.0, 1.0),
    )
    val alumbra_token_color_ssurface_soverlay: List<Color> = listOf(
      srgb(0.118, 0.161, 0.231),
      srgb(0.0, 0.0, 0.0),
      srgb(0.945, 0.961, 0.976),
      srgb(1.0, 1.0, 1.0),
    )
    val alumbra_token_color_ssurface_sscrim: List<Color> = listOf(
      srgb(0.0, 0.0, 0.0, 0.72),
    )
    val alumbra_token_color_stext_sdisabled: List<Color> = listOf(
      srgb(0.392, 0.455, 0.545),
    )
    val alumbra_token_color_stext_sprimary: List<Color> = listOf(
      srgb(0.973, 0.98, 0.988),
      srgb(1.0, 1.0, 1.0),
      srgb(0.059, 0.09, 0.165),
      srgb(0.0, 0.0, 0.0),
    )
    val alumbra_token_color_stext_ssecondary: List<Color> = listOf(
      srgb(0.796, 0.835, 0.882),
      srgb(1.0, 1.0, 1.0),
      srgb(0.2, 0.255, 0.333),
      srgb(0.0, 0.0, 0.0),
    )
    val alumbra_token_elevation_smodal: List<List<ShadowLayer>> = listOf(
      listOf(layer(srgb(0.0, 0.0, 0.0), 0.0, 8.0, 24.0, 0.0, false)),
    )
    val alumbra_token_elevation_spanel: List<List<ShadowLayer>> = listOf(
      listOf(layer(srgb(0.0, 0.0, 0.0), 0.0, 4.0, 12.0, 0.0, false)),
    )
    val alumbra_token_elevation_sraised: List<List<ShadowLayer>> = listOf(
      listOf(layer(srgb(0.0, 0.0, 0.0), 0.0, 2.0, 6.0, 0.0, false)),
    )
    val alumbra_token_layer_sad_uchrome: List<Double> = listOf(
      30.0,
    )
    val alumbra_token_layer_sad_usdk_ucontainer: List<Double> = listOf(
      40.0,
    )
    val alumbra_token_layer_scaptions: List<Double> = listOf(
      10.0,
    )
    val alumbra_token_layer_scontrols: List<Double> = listOf(
      20.0,
    )
    val alumbra_token_layer_svideo_usurface: List<Double> = listOf(
      0.0,
    )
    val alumbra_token_luminance_shdr_uhighlight_uceiling: List<Double> = listOf(
      203.0,
      600.0,
    )
    val alumbra_token_luminance_sreference_uwhite: List<Double> = listOf(
      203.0,
    )
    val alumbra_token_motion_sduration_sinstant: List<Double> = listOf(
      0.0,
    )
    val alumbra_token_motion_sduration_slong: List<Double> = listOf(
      320.0,
      0.0,
    )
    val alumbra_token_motion_sduration_smedium: List<Double> = listOf(
      200.0,
      0.0,
    )
    val alumbra_token_motion_sduration_sshort: List<Double> = listOf(
      120.0,
      0.0,
    )
    val alumbra_token_motion_seasing_semphasized: List<CubicBezier> = listOf(
      bezier(0.2, 0.0, 0.0, 1.4),
    )
    val alumbra_token_motion_seasing_sstandard: List<CubicBezier> = listOf(
      bezier(0.2, 0.0, 0.0, 1.0),
    )
    val alumbra_token_opacity_sdisabled: List<Double> = listOf(
      0.48,
    )
    val alumbra_token_radius_slarge: List<Double> = listOf(
      16.0,
    )
    val alumbra_token_radius_smedium: List<Double> = listOf(
      8.0,
    )
    val alumbra_token_radius_spill: List<Double> = listOf(
      999.0,
    )
    val alumbra_token_radius_ssmall: List<Double> = listOf(
      4.0,
    )
    val alumbra_token_spacing_sextra_ularge: List<Double> = listOf(
      28.0,
      36.0,
      32.0,
      56.0,
    )
    val alumbra_token_spacing_slarge: List<Double> = listOf(
      20.0,
      28.0,
      24.0,
      40.0,
    )
    val alumbra_token_spacing_smedium: List<Double> = listOf(
      12.0,
      20.0,
      16.0,
      28.0,
    )
    val alumbra_token_spacing_snone: List<Double> = listOf(
      0.0,
    )
    val alumbra_token_spacing_ssmall: List<Double> = listOf(
      8.0,
      8.0,
      8.0,
      16.0,
    )
    val alumbra_token_spacing_stight: List<Double> = listOf(
      4.0,
      4.0,
      4.0,
      8.0,
    )
    val alumbra_token_target_sminimum: List<Double> = listOf(
      48.0,
      48.0,
      44.0,
      72.0,
    )
    val alumbra_token_typography_sfont_ufamily_sui: List<List<String>> = listOf(
      listOf("system-ui"),
    )
    val alumbra_token_typography_sfont_usize_sbody: List<Double> = listOf(
      17.0,
      18.0,
      16.0,
      24.0,
    )
    val alumbra_token_typography_sfont_usize_scaption: List<Double> = listOf(
      13.0,
      14.0,
      12.0,
      18.0,
    )
    val alumbra_token_typography_sfont_usize_sdisplay: List<Double> = listOf(
      34.0,
      36.0,
      32.0,
      52.0,
    )
    val alumbra_token_typography_sfont_usize_stitle: List<Double> = listOf(
      25.0,
      26.0,
      24.0,
      36.0,
    )
    val alumbra_token_typography_sfont_uweight_sbold: List<Double> = listOf(
      700.0,
    )
    val alumbra_token_typography_sfont_uweight_smedium: List<Double> = listOf(
      500.0,
    )
    val alumbra_token_typography_sfont_uweight_sregular: List<Double> = listOf(
      400.0,
    )
    val alumbra_token_typography_sline_uheight_sbody: List<Double> = listOf(
      1.5,
    )
    val alumbra_token_typography_sline_uheight_scompact: List<Double> = listOf(
      1.2,
    )
}
