#if canImport(SwiftUI)
  import SwiftUI
#endif

public enum DesignTokens {
  public enum Theme: String, CaseIterable, Sendable {
    case dark
    case light

    var position: Int {
      switch self {
      case .dark: 0
      case .light: 1
      }
    }
  }

  public enum Contrast: String, CaseIterable, Sendable {
    case normal
    case high

    var position: Int {
      switch self {
      case .normal: 0
      case .high: 1
      }
    }
  }

  public enum Density: String, CaseIterable, Sendable {
    case phone
    case tablet
    case desktop
    case tv

    var position: Int {
      switch self {
      case .phone: 0
      case .tablet: 1
      case .desktop: 2
      case .tv: 3
      }
    }
  }

  public enum Motion: String, CaseIterable, Sendable {
    case full
    case reduced

    var position: Int {
      switch self {
      case .full: 0
      case .reduced: 1
      }
    }
  }

  public enum DynamicRange: String, CaseIterable, Sendable {
    case sdr
    case hdr

    var position: Int {
      switch self {
      case .sdr: 0
      case .hdr: 1
      }
    }
  }

  public struct Context: Equatable, Sendable {
    public var theme: Theme
    public var contrast: Contrast
    public var density: Density
    public var motion: Motion
    public var dynamicRange: DynamicRange

    public init(
      theme: Theme = .dark,
      contrast: Contrast = .normal,
      density: Density = .desktop,
      motion: Motion = .full,
      dynamicRange: DynamicRange = .sdr
    ) {
      self.theme = theme
      self.contrast = contrast
      self.density = density
      self.motion = motion
      self.dynamicRange = dynamicRange
    }
  }

  public enum ColorSpace: String, Sendable {
    case srgb
    case displayP3 = "display-p3"
    case rec2020
  }

  public struct Rgb: Equatable, Sendable {
    public let red: Double
    public let green: Double
    public let blue: Double

    public init(red: Double, green: Double, blue: Double) {
      self.red = red
      self.green = green
      self.blue = blue
    }
  }

  public struct Color: Equatable, Sendable {
    public let colorSpace: ColorSpace
    public let components: Rgb
    public let alpha: Double
    public let srgb: Rgb

    public init(colorSpace: ColorSpace, components: Rgb, alpha: Double, srgb: Rgb) {
      self.colorSpace = colorSpace
      self.components = components
      self.alpha = alpha
      self.srgb = srgb
    }
  }

  public struct CubicBezier: Equatable, Sendable {
    public let x1: Double
    public let y1: Double
    public let x2: Double
    public let y2: Double

    public init(x1: Double, y1: Double, x2: Double, y2: Double) {
      self.x1 = x1
      self.y1 = y1
      self.x2 = x2
      self.y2 = y2
    }
  }

  public struct ShadowLayer: Equatable, Sendable {
    public let color: Color
    public let offsetX: Double
    public let offsetY: Double
    public let blur: Double
    public let spread: Double
    public let inset: Bool

    public init(
      color: Color, offsetX: Double, offsetY: Double, blur: Double, spread: Double, inset: Bool
    ) {
      self.color = color
      self.offsetX = offsetX
      self.offsetY = offsetY
      self.blur = blur
      self.spread = spread
      self.inset = inset
    }
  }

  public enum Value: Equatable, Sendable {
    case color(Color)
    case dimension(Double)
    case duration(Double)
    case number(Double)
    case fontWeight(Double)
    case fontFamily([String])
    case cubicBezier(CubicBezier)
    case shadow([ShadowLayer])
  }

  public struct Overrides: Sendable {
    public var alumbra_token_color_saccent_splayed: Color?
    public var alumbra_token_color_sborder_ssubtle: Color?
    public var alumbra_token_color_scontrol_sbackground: Color?
    public var alumbra_token_color_scontrol_sdisabled: Color?
    public var alumbra_token_color_scontrol_sforeground: Color?
    public var alumbra_token_color_scontrol_shover: Color?
    public var alumbra_token_color_scontrol_spressed: Color?
    public var alumbra_token_color_sfocus_sring: Color?
    public var alumbra_token_color_ssurface_sbase: Color?
    public var alumbra_token_color_ssurface_selevated: Color?
    public var alumbra_token_color_ssurface_soverlay: Color?
    public var alumbra_token_color_ssurface_sscrim: Color?
    public var alumbra_token_color_stext_sdisabled: Color?
    public var alumbra_token_color_stext_sprimary: Color?
    public var alumbra_token_color_stext_ssecondary: Color?
    public var alumbra_token_elevation_smodal: [ShadowLayer]?
    public var alumbra_token_elevation_spanel: [ShadowLayer]?
    public var alumbra_token_elevation_sraised: [ShadowLayer]?
    public var alumbra_token_layer_sad_uchrome: Double?
    public var alumbra_token_layer_sad_usdk_ucontainer: Double?
    public var alumbra_token_layer_scaptions: Double?
    public var alumbra_token_layer_scontrols: Double?
    public var alumbra_token_layer_svideo_usurface: Double?
    public var alumbra_token_luminance_shdr_uhighlight_uceiling: Double?
    public var alumbra_token_luminance_sreference_uwhite: Double?
    public var alumbra_token_motion_sduration_sinstant: Double?
    public var alumbra_token_motion_sduration_slong: Double?
    public var alumbra_token_motion_sduration_smedium: Double?
    public var alumbra_token_motion_sduration_sshort: Double?
    public var alumbra_token_motion_seasing_semphasized: CubicBezier?
    public var alumbra_token_motion_seasing_sstandard: CubicBezier?
    public var alumbra_token_opacity_sdisabled: Double?
    public var alumbra_token_radius_slarge: Double?
    public var alumbra_token_radius_smedium: Double?
    public var alumbra_token_radius_spill: Double?
    public var alumbra_token_radius_ssmall: Double?
    public var alumbra_token_spacing_sextra_ularge: Double?
    public var alumbra_token_spacing_slarge: Double?
    public var alumbra_token_spacing_smedium: Double?
    public var alumbra_token_spacing_snone: Double?
    public var alumbra_token_spacing_ssmall: Double?
    public var alumbra_token_spacing_stight: Double?
    public var alumbra_token_target_sminimum: Double?
    public var alumbra_token_typography_sfont_ufamily_sui: [String]?
    public var alumbra_token_typography_sfont_usize_sbody: Double?
    public var alumbra_token_typography_sfont_usize_scaption: Double?
    public var alumbra_token_typography_sfont_usize_sdisplay: Double?
    public var alumbra_token_typography_sfont_usize_stitle: Double?
    public var alumbra_token_typography_sfont_uweight_sbold: Double?
    public var alumbra_token_typography_sfont_uweight_smedium: Double?
    public var alumbra_token_typography_sfont_uweight_sregular: Double?
    public var alumbra_token_typography_sline_uheight_sbody: Double?
    public var alumbra_token_typography_sline_uheight_scompact: Double?
    public var alumbra_token_player_sad_sband_sbackground: Color?
    public var alumbra_token_player_sad_uband_sbackground: Color?
    public var alumbra_token_player_sad_uchrome_sbadge_sbackground: Color?
    public var alumbra_token_player_sbutton_sbackground: Color?
    public var alumbra_token_player_sbutton_sbackground_sdisabled: Color?
    public var alumbra_token_player_sbutton_sbackground_sfocus: Color?
    public var alumbra_token_player_sbutton_sbackground_shover: Color?
    public var alumbra_token_player_sbutton_sbackground_spressed: Color?
    public var alumbra_token_player_sbutton_sforeground: Color?
    public var alumbra_token_player_sbutton_sforeground_sdisabled: Color?
    public var alumbra_token_player_sbutton_sforeground_sfocus: Color?
    public var alumbra_token_player_sbutton_sforeground_shover: Color?
    public var alumbra_token_player_sbutton_sforeground_spressed: Color?
    public var alumbra_token_player_sbutton_sradius: Double?
    public var alumbra_token_player_scaptions_sfont_usize: Double?
    public var alumbra_token_player_scaptions_stext: Color?
    public var alumbra_token_player_scaptions_swindow: Color?
    public var alumbra_token_player_sfocus_sring: Color?
    public var alumbra_token_player_sseekbar_splayed: Color?
    public var alumbra_token_player_sseekbar_strack: Color?

    public init() {}
  }

  public struct Tokens: Sendable {
    public let alumbra_token_color_saccent_splayed: Color
    public let alumbra_token_color_sborder_ssubtle: Color
    public let alumbra_token_color_scontrol_sbackground: Color
    public let alumbra_token_color_scontrol_sdisabled: Color
    public let alumbra_token_color_scontrol_sforeground: Color
    public let alumbra_token_color_scontrol_shover: Color
    public let alumbra_token_color_scontrol_spressed: Color
    public let alumbra_token_color_sfocus_sring: Color
    public let alumbra_token_color_ssurface_sbase: Color
    public let alumbra_token_color_ssurface_selevated: Color
    public let alumbra_token_color_ssurface_soverlay: Color
    public let alumbra_token_color_ssurface_sscrim: Color
    public let alumbra_token_color_stext_sdisabled: Color
    public let alumbra_token_color_stext_sprimary: Color
    public let alumbra_token_color_stext_ssecondary: Color
    public let alumbra_token_elevation_smodal: [ShadowLayer]
    public let alumbra_token_elevation_spanel: [ShadowLayer]
    public let alumbra_token_elevation_sraised: [ShadowLayer]
    public let alumbra_token_layer_sad_uchrome: Double
    public let alumbra_token_layer_sad_usdk_ucontainer: Double
    public let alumbra_token_layer_scaptions: Double
    public let alumbra_token_layer_scontrols: Double
    public let alumbra_token_layer_svideo_usurface: Double
    public let alumbra_token_luminance_shdr_uhighlight_uceiling: Double
    public let alumbra_token_luminance_sreference_uwhite: Double
    public let alumbra_token_motion_sduration_sinstant: Double
    public let alumbra_token_motion_sduration_slong: Double
    public let alumbra_token_motion_sduration_smedium: Double
    public let alumbra_token_motion_sduration_sshort: Double
    public let alumbra_token_motion_seasing_semphasized: CubicBezier
    public let alumbra_token_motion_seasing_sstandard: CubicBezier
    public let alumbra_token_opacity_sdisabled: Double
    public let alumbra_token_radius_slarge: Double
    public let alumbra_token_radius_smedium: Double
    public let alumbra_token_radius_spill: Double
    public let alumbra_token_radius_ssmall: Double
    public let alumbra_token_spacing_sextra_ularge: Double
    public let alumbra_token_spacing_slarge: Double
    public let alumbra_token_spacing_smedium: Double
    public let alumbra_token_spacing_snone: Double
    public let alumbra_token_spacing_ssmall: Double
    public let alumbra_token_spacing_stight: Double
    public let alumbra_token_target_sminimum: Double
    public let alumbra_token_typography_sfont_ufamily_sui: [String]
    public let alumbra_token_typography_sfont_usize_sbody: Double
    public let alumbra_token_typography_sfont_usize_scaption: Double
    public let alumbra_token_typography_sfont_usize_sdisplay: Double
    public let alumbra_token_typography_sfont_usize_stitle: Double
    public let alumbra_token_typography_sfont_uweight_sbold: Double
    public let alumbra_token_typography_sfont_uweight_smedium: Double
    public let alumbra_token_typography_sfont_uweight_sregular: Double
    public let alumbra_token_typography_sline_uheight_sbody: Double
    public let alumbra_token_typography_sline_uheight_scompact: Double
    public let alumbra_token_player_sad_sband_sbackground: Color
    public let alumbra_token_player_sad_uband_sbackground: Color
    public let alumbra_token_player_sad_uchrome_sbadge_sbackground: Color
    public let alumbra_token_player_sbutton_sbackground: Color
    public let alumbra_token_player_sbutton_sbackground_sdisabled: Color
    public let alumbra_token_player_sbutton_sbackground_sfocus: Color
    public let alumbra_token_player_sbutton_sbackground_shover: Color
    public let alumbra_token_player_sbutton_sbackground_spressed: Color
    public let alumbra_token_player_sbutton_sforeground: Color
    public let alumbra_token_player_sbutton_sforeground_sdisabled: Color
    public let alumbra_token_player_sbutton_sforeground_sfocus: Color
    public let alumbra_token_player_sbutton_sforeground_shover: Color
    public let alumbra_token_player_sbutton_sforeground_spressed: Color
    public let alumbra_token_player_sbutton_sradius: Double
    public let alumbra_token_player_scaptions_sfont_usize: Double
    public let alumbra_token_player_scaptions_stext: Color
    public let alumbra_token_player_scaptions_swindow: Color
    public let alumbra_token_player_sfocus_sring: Color
    public let alumbra_token_player_sseekbar_splayed: Color
    public let alumbra_token_player_sseekbar_strack: Color

    public init(context: Context = Context(), overrides: Overrides = Overrides()) {
      alumbra_token_color_saccent_splayed =
        overrides.alumbra_token_color_saccent_splayed
        ?? select(Defaults.alumbra_token_color_saccent_splayed, context, [])
      alumbra_token_color_sborder_ssubtle =
        overrides.alumbra_token_color_sborder_ssubtle
        ?? select(Defaults.alumbra_token_color_sborder_ssubtle, context, [.theme, .contrast])
      alumbra_token_color_scontrol_sbackground =
        overrides.alumbra_token_color_scontrol_sbackground
        ?? select(Defaults.alumbra_token_color_scontrol_sbackground, context, [.theme, .contrast])
      alumbra_token_color_scontrol_sdisabled =
        overrides.alumbra_token_color_scontrol_sdisabled
        ?? select(Defaults.alumbra_token_color_scontrol_sdisabled, context, [.theme, .contrast])
      alumbra_token_color_scontrol_sforeground =
        overrides.alumbra_token_color_scontrol_sforeground
        ?? select(Defaults.alumbra_token_color_scontrol_sforeground, context, [.theme, .contrast])
      alumbra_token_color_scontrol_shover =
        overrides.alumbra_token_color_scontrol_shover
        ?? select(Defaults.alumbra_token_color_scontrol_shover, context, [.theme, .contrast])
      alumbra_token_color_scontrol_spressed =
        overrides.alumbra_token_color_scontrol_spressed
        ?? select(Defaults.alumbra_token_color_scontrol_spressed, context, [.theme, .contrast])
      alumbra_token_color_sfocus_sring =
        overrides.alumbra_token_color_sfocus_sring
        ?? select(Defaults.alumbra_token_color_sfocus_sring, context, [.theme, .contrast])
      alumbra_token_color_ssurface_sbase =
        overrides.alumbra_token_color_ssurface_sbase
        ?? select(Defaults.alumbra_token_color_ssurface_sbase, context, [.theme, .contrast])
      alumbra_token_color_ssurface_selevated =
        overrides.alumbra_token_color_ssurface_selevated
        ?? select(Defaults.alumbra_token_color_ssurface_selevated, context, [.theme, .contrast])
      alumbra_token_color_ssurface_soverlay =
        overrides.alumbra_token_color_ssurface_soverlay
        ?? select(Defaults.alumbra_token_color_ssurface_soverlay, context, [.theme, .contrast])
      alumbra_token_color_ssurface_sscrim =
        overrides.alumbra_token_color_ssurface_sscrim
        ?? select(Defaults.alumbra_token_color_ssurface_sscrim, context, [])
      alumbra_token_color_stext_sdisabled =
        overrides.alumbra_token_color_stext_sdisabled
        ?? select(Defaults.alumbra_token_color_stext_sdisabled, context, [])
      alumbra_token_color_stext_sprimary =
        overrides.alumbra_token_color_stext_sprimary
        ?? select(Defaults.alumbra_token_color_stext_sprimary, context, [.theme, .contrast])
      alumbra_token_color_stext_ssecondary =
        overrides.alumbra_token_color_stext_ssecondary
        ?? select(Defaults.alumbra_token_color_stext_ssecondary, context, [.theme, .contrast])
      alumbra_token_elevation_smodal =
        overrides.alumbra_token_elevation_smodal
        ?? select(Defaults.alumbra_token_elevation_smodal, context, [])
      alumbra_token_elevation_spanel =
        overrides.alumbra_token_elevation_spanel
        ?? select(Defaults.alumbra_token_elevation_spanel, context, [])
      alumbra_token_elevation_sraised =
        overrides.alumbra_token_elevation_sraised
        ?? select(Defaults.alumbra_token_elevation_sraised, context, [])
      alumbra_token_layer_sad_uchrome =
        overrides.alumbra_token_layer_sad_uchrome
        ?? select(Defaults.alumbra_token_layer_sad_uchrome, context, [])
      alumbra_token_layer_sad_usdk_ucontainer =
        overrides.alumbra_token_layer_sad_usdk_ucontainer
        ?? select(Defaults.alumbra_token_layer_sad_usdk_ucontainer, context, [])
      alumbra_token_layer_scaptions =
        overrides.alumbra_token_layer_scaptions
        ?? select(Defaults.alumbra_token_layer_scaptions, context, [])
      alumbra_token_layer_scontrols =
        overrides.alumbra_token_layer_scontrols
        ?? select(Defaults.alumbra_token_layer_scontrols, context, [])
      alumbra_token_layer_svideo_usurface =
        overrides.alumbra_token_layer_svideo_usurface
        ?? select(Defaults.alumbra_token_layer_svideo_usurface, context, [])
      alumbra_token_luminance_shdr_uhighlight_uceiling =
        overrides.alumbra_token_luminance_shdr_uhighlight_uceiling
        ?? select(Defaults.alumbra_token_luminance_shdr_uhighlight_uceiling, context, [.dynamicRange])
      alumbra_token_luminance_sreference_uwhite =
        overrides.alumbra_token_luminance_sreference_uwhite
        ?? select(Defaults.alumbra_token_luminance_sreference_uwhite, context, [])
      alumbra_token_motion_sduration_sinstant =
        overrides.alumbra_token_motion_sduration_sinstant
        ?? select(Defaults.alumbra_token_motion_sduration_sinstant, context, [])
      alumbra_token_motion_sduration_slong =
        overrides.alumbra_token_motion_sduration_slong
        ?? select(Defaults.alumbra_token_motion_sduration_slong, context, [.motion])
      alumbra_token_motion_sduration_smedium =
        overrides.alumbra_token_motion_sduration_smedium
        ?? select(Defaults.alumbra_token_motion_sduration_smedium, context, [.motion])
      alumbra_token_motion_sduration_sshort =
        overrides.alumbra_token_motion_sduration_sshort
        ?? select(Defaults.alumbra_token_motion_sduration_sshort, context, [.motion])
      alumbra_token_motion_seasing_semphasized =
        overrides.alumbra_token_motion_seasing_semphasized
        ?? select(Defaults.alumbra_token_motion_seasing_semphasized, context, [])
      alumbra_token_motion_seasing_sstandard =
        overrides.alumbra_token_motion_seasing_sstandard
        ?? select(Defaults.alumbra_token_motion_seasing_sstandard, context, [])
      alumbra_token_opacity_sdisabled =
        overrides.alumbra_token_opacity_sdisabled
        ?? select(Defaults.alumbra_token_opacity_sdisabled, context, [])
      alumbra_token_radius_slarge =
        overrides.alumbra_token_radius_slarge
        ?? select(Defaults.alumbra_token_radius_slarge, context, [])
      alumbra_token_radius_smedium =
        overrides.alumbra_token_radius_smedium
        ?? select(Defaults.alumbra_token_radius_smedium, context, [])
      alumbra_token_radius_spill =
        overrides.alumbra_token_radius_spill
        ?? select(Defaults.alumbra_token_radius_spill, context, [])
      alumbra_token_radius_ssmall =
        overrides.alumbra_token_radius_ssmall
        ?? select(Defaults.alumbra_token_radius_ssmall, context, [])
      alumbra_token_spacing_sextra_ularge =
        overrides.alumbra_token_spacing_sextra_ularge
        ?? select(Defaults.alumbra_token_spacing_sextra_ularge, context, [.density])
      alumbra_token_spacing_slarge =
        overrides.alumbra_token_spacing_slarge
        ?? select(Defaults.alumbra_token_spacing_slarge, context, [.density])
      alumbra_token_spacing_smedium =
        overrides.alumbra_token_spacing_smedium
        ?? select(Defaults.alumbra_token_spacing_smedium, context, [.density])
      alumbra_token_spacing_snone =
        overrides.alumbra_token_spacing_snone
        ?? select(Defaults.alumbra_token_spacing_snone, context, [])
      alumbra_token_spacing_ssmall =
        overrides.alumbra_token_spacing_ssmall
        ?? select(Defaults.alumbra_token_spacing_ssmall, context, [.density])
      alumbra_token_spacing_stight =
        overrides.alumbra_token_spacing_stight
        ?? select(Defaults.alumbra_token_spacing_stight, context, [.density])
      alumbra_token_target_sminimum =
        overrides.alumbra_token_target_sminimum
        ?? select(Defaults.alumbra_token_target_sminimum, context, [.density])
      alumbra_token_typography_sfont_ufamily_sui =
        overrides.alumbra_token_typography_sfont_ufamily_sui
        ?? select(Defaults.alumbra_token_typography_sfont_ufamily_sui, context, [])
      alumbra_token_typography_sfont_usize_sbody =
        overrides.alumbra_token_typography_sfont_usize_sbody
        ?? select(Defaults.alumbra_token_typography_sfont_usize_sbody, context, [.density])
      alumbra_token_typography_sfont_usize_scaption =
        overrides.alumbra_token_typography_sfont_usize_scaption
        ?? select(Defaults.alumbra_token_typography_sfont_usize_scaption, context, [.density])
      alumbra_token_typography_sfont_usize_sdisplay =
        overrides.alumbra_token_typography_sfont_usize_sdisplay
        ?? select(Defaults.alumbra_token_typography_sfont_usize_sdisplay, context, [.density])
      alumbra_token_typography_sfont_usize_stitle =
        overrides.alumbra_token_typography_sfont_usize_stitle
        ?? select(Defaults.alumbra_token_typography_sfont_usize_stitle, context, [.density])
      alumbra_token_typography_sfont_uweight_sbold =
        overrides.alumbra_token_typography_sfont_uweight_sbold
        ?? select(Defaults.alumbra_token_typography_sfont_uweight_sbold, context, [])
      alumbra_token_typography_sfont_uweight_smedium =
        overrides.alumbra_token_typography_sfont_uweight_smedium
        ?? select(Defaults.alumbra_token_typography_sfont_uweight_smedium, context, [])
      alumbra_token_typography_sfont_uweight_sregular =
        overrides.alumbra_token_typography_sfont_uweight_sregular
        ?? select(Defaults.alumbra_token_typography_sfont_uweight_sregular, context, [])
      alumbra_token_typography_sline_uheight_sbody =
        overrides.alumbra_token_typography_sline_uheight_sbody
        ?? select(Defaults.alumbra_token_typography_sline_uheight_sbody, context, [])
      alumbra_token_typography_sline_uheight_scompact =
        overrides.alumbra_token_typography_sline_uheight_scompact
        ?? select(Defaults.alumbra_token_typography_sline_uheight_scompact, context, [])
      alumbra_token_player_sad_sband_sbackground =
        overrides.alumbra_token_player_sad_sband_sbackground
        ?? alumbra_token_color_ssurface_selevated
      alumbra_token_player_sad_uband_sbackground =
        overrides.alumbra_token_player_sad_uband_sbackground
        ?? alumbra_token_color_ssurface_soverlay
      alumbra_token_player_sad_uchrome_sbadge_sbackground =
        overrides.alumbra_token_player_sad_uchrome_sbadge_sbackground
        ?? alumbra_token_color_scontrol_sbackground
      alumbra_token_player_sbutton_sbackground =
        overrides.alumbra_token_player_sbutton_sbackground
        ?? alumbra_token_color_scontrol_sbackground
      alumbra_token_player_sbutton_sbackground_sdisabled =
        overrides.alumbra_token_player_sbutton_sbackground_sdisabled
        ?? alumbra_token_color_scontrol_sdisabled
      alumbra_token_player_sbutton_sbackground_sfocus =
        overrides.alumbra_token_player_sbutton_sbackground_sfocus
        ?? alumbra_token_color_scontrol_sbackground
      alumbra_token_player_sbutton_sbackground_shover =
        overrides.alumbra_token_player_sbutton_sbackground_shover
        ?? alumbra_token_color_scontrol_shover
      alumbra_token_player_sbutton_sbackground_spressed =
        overrides.alumbra_token_player_sbutton_sbackground_spressed
        ?? alumbra_token_color_scontrol_spressed
      alumbra_token_player_sbutton_sforeground =
        overrides.alumbra_token_player_sbutton_sforeground
        ?? alumbra_token_color_scontrol_sforeground
      alumbra_token_player_sbutton_sforeground_sdisabled =
        overrides.alumbra_token_player_sbutton_sforeground_sdisabled
        ?? alumbra_token_color_stext_sdisabled
      alumbra_token_player_sbutton_sforeground_sfocus =
        overrides.alumbra_token_player_sbutton_sforeground_sfocus
        ?? alumbra_token_color_scontrol_sforeground
      alumbra_token_player_sbutton_sforeground_shover =
        overrides.alumbra_token_player_sbutton_sforeground_shover
        ?? alumbra_token_color_scontrol_sforeground
      alumbra_token_player_sbutton_sforeground_spressed =
        overrides.alumbra_token_player_sbutton_sforeground_spressed
        ?? alumbra_token_color_scontrol_sforeground
      alumbra_token_player_sbutton_sradius =
        overrides.alumbra_token_player_sbutton_sradius
        ?? alumbra_token_radius_smedium
      alumbra_token_player_scaptions_sfont_usize =
        overrides.alumbra_token_player_scaptions_sfont_usize
        ?? alumbra_token_typography_sfont_usize_scaption
      alumbra_token_player_scaptions_stext =
        overrides.alumbra_token_player_scaptions_stext
        ?? alumbra_token_color_stext_sprimary
      alumbra_token_player_scaptions_swindow =
        overrides.alumbra_token_player_scaptions_swindow
        ?? alumbra_token_color_ssurface_sscrim
      alumbra_token_player_sfocus_sring =
        overrides.alumbra_token_player_sfocus_sring
        ?? alumbra_token_color_sfocus_sring
      alumbra_token_player_sseekbar_splayed =
        overrides.alumbra_token_player_sseekbar_splayed
        ?? alumbra_token_color_saccent_splayed
      alumbra_token_player_sseekbar_strack =
        overrides.alumbra_token_player_sseekbar_strack
        ?? alumbra_token_color_sborder_ssubtle
    }

    public var entries: [(name: String, value: Value)] {
      [
        ("color.accent.played", .color(alumbra_token_color_saccent_splayed)),
        ("color.border.subtle", .color(alumbra_token_color_sborder_ssubtle)),
        ("color.control.background", .color(alumbra_token_color_scontrol_sbackground)),
        ("color.control.disabled", .color(alumbra_token_color_scontrol_sdisabled)),
        ("color.control.foreground", .color(alumbra_token_color_scontrol_sforeground)),
        ("color.control.hover", .color(alumbra_token_color_scontrol_shover)),
        ("color.control.pressed", .color(alumbra_token_color_scontrol_spressed)),
        ("color.focus.ring", .color(alumbra_token_color_sfocus_sring)),
        ("color.surface.base", .color(alumbra_token_color_ssurface_sbase)),
        ("color.surface.elevated", .color(alumbra_token_color_ssurface_selevated)),
        ("color.surface.overlay", .color(alumbra_token_color_ssurface_soverlay)),
        ("color.surface.scrim", .color(alumbra_token_color_ssurface_sscrim)),
        ("color.text.disabled", .color(alumbra_token_color_stext_sdisabled)),
        ("color.text.primary", .color(alumbra_token_color_stext_sprimary)),
        ("color.text.secondary", .color(alumbra_token_color_stext_ssecondary)),
        ("elevation.modal", .shadow(alumbra_token_elevation_smodal)),
        ("elevation.panel", .shadow(alumbra_token_elevation_spanel)),
        ("elevation.raised", .shadow(alumbra_token_elevation_sraised)),
        ("layer.ad_chrome", .number(alumbra_token_layer_sad_uchrome)),
        ("layer.ad_sdk_container", .number(alumbra_token_layer_sad_usdk_ucontainer)),
        ("layer.captions", .number(alumbra_token_layer_scaptions)),
        ("layer.controls", .number(alumbra_token_layer_scontrols)),
        ("layer.video_surface", .number(alumbra_token_layer_svideo_usurface)),
        ("luminance.hdr_highlight_ceiling", .number(alumbra_token_luminance_shdr_uhighlight_uceiling)),
        ("luminance.reference_white", .number(alumbra_token_luminance_sreference_uwhite)),
        ("motion.duration.instant", .duration(alumbra_token_motion_sduration_sinstant)),
        ("motion.duration.long", .duration(alumbra_token_motion_sduration_slong)),
        ("motion.duration.medium", .duration(alumbra_token_motion_sduration_smedium)),
        ("motion.duration.short", .duration(alumbra_token_motion_sduration_sshort)),
        ("motion.easing.emphasized", .cubicBezier(alumbra_token_motion_seasing_semphasized)),
        ("motion.easing.standard", .cubicBezier(alumbra_token_motion_seasing_sstandard)),
        ("opacity.disabled", .number(alumbra_token_opacity_sdisabled)),
        ("radius.large", .dimension(alumbra_token_radius_slarge)),
        ("radius.medium", .dimension(alumbra_token_radius_smedium)),
        ("radius.pill", .dimension(alumbra_token_radius_spill)),
        ("radius.small", .dimension(alumbra_token_radius_ssmall)),
        ("spacing.extra_large", .dimension(alumbra_token_spacing_sextra_ularge)),
        ("spacing.large", .dimension(alumbra_token_spacing_slarge)),
        ("spacing.medium", .dimension(alumbra_token_spacing_smedium)),
        ("spacing.none", .dimension(alumbra_token_spacing_snone)),
        ("spacing.small", .dimension(alumbra_token_spacing_ssmall)),
        ("spacing.tight", .dimension(alumbra_token_spacing_stight)),
        ("target.minimum", .dimension(alumbra_token_target_sminimum)),
        ("typography.font_family.ui", .fontFamily(alumbra_token_typography_sfont_ufamily_sui)),
        ("typography.font_size.body", .dimension(alumbra_token_typography_sfont_usize_sbody)),
        ("typography.font_size.caption", .dimension(alumbra_token_typography_sfont_usize_scaption)),
        ("typography.font_size.display", .dimension(alumbra_token_typography_sfont_usize_sdisplay)),
        ("typography.font_size.title", .dimension(alumbra_token_typography_sfont_usize_stitle)),
        ("typography.font_weight.bold", .fontWeight(alumbra_token_typography_sfont_uweight_sbold)),
        ("typography.font_weight.medium", .fontWeight(alumbra_token_typography_sfont_uweight_smedium)),
        ("typography.font_weight.regular", .fontWeight(alumbra_token_typography_sfont_uweight_sregular)),
        ("typography.line_height.body", .number(alumbra_token_typography_sline_uheight_sbody)),
        ("typography.line_height.compact", .number(alumbra_token_typography_sline_uheight_scompact)),
        ("player.ad.band.background", .color(alumbra_token_player_sad_sband_sbackground)),
        ("player.ad_band.background", .color(alumbra_token_player_sad_uband_sbackground)),
        ("player.ad_chrome.badge.background", .color(alumbra_token_player_sad_uchrome_sbadge_sbackground)),
        ("player.button.background", .color(alumbra_token_player_sbutton_sbackground)),
        ("player.button.background.disabled", .color(alumbra_token_player_sbutton_sbackground_sdisabled)),
        ("player.button.background.focus", .color(alumbra_token_player_sbutton_sbackground_sfocus)),
        ("player.button.background.hover", .color(alumbra_token_player_sbutton_sbackground_shover)),
        ("player.button.background.pressed", .color(alumbra_token_player_sbutton_sbackground_spressed)),
        ("player.button.foreground", .color(alumbra_token_player_sbutton_sforeground)),
        ("player.button.foreground.disabled", .color(alumbra_token_player_sbutton_sforeground_sdisabled)),
        ("player.button.foreground.focus", .color(alumbra_token_player_sbutton_sforeground_sfocus)),
        ("player.button.foreground.hover", .color(alumbra_token_player_sbutton_sforeground_shover)),
        ("player.button.foreground.pressed", .color(alumbra_token_player_sbutton_sforeground_spressed)),
        ("player.button.radius", .dimension(alumbra_token_player_sbutton_sradius)),
        ("player.captions.font_size", .dimension(alumbra_token_player_scaptions_sfont_usize)),
        ("player.captions.text", .color(alumbra_token_player_scaptions_stext)),
        ("player.captions.window", .color(alumbra_token_player_scaptions_swindow)),
        ("player.focus.ring", .color(alumbra_token_player_sfocus_sring)),
        ("player.seekbar.played", .color(alumbra_token_player_sseekbar_splayed)),
        ("player.seekbar.track", .color(alumbra_token_player_sseekbar_strack)),
      ]
    }
  }

  enum Modifier {
    case theme
    case contrast
    case density
    case motion
    case dynamicRange
  }

  static func select<T>(_ values: [T], _ context: Context, _ modifiers: [Modifier]) -> T {
    var index = 0
    for modifier in modifiers {
      switch modifier {
      case .theme:
        index = index * Theme.allCases.count + context.theme.position
      case .contrast:
        index = index * Contrast.allCases.count + context.contrast.position
      case .density:
        index = index * Density.allCases.count + context.density.position
      case .motion:
        index = index * Motion.allCases.count + context.motion.position
      case .dynamicRange:
        index = index * DynamicRange.allCases.count + context.dynamicRange.position
      }
    }
    return values[index]
  }

  static func srgb(_ red: Double, _ green: Double, _ blue: Double, _ alpha: Double = 1)
    -> Color
  {
    let components = Rgb(red: red, green: green, blue: blue)
    return Color(colorSpace: .srgb, components: components, alpha: alpha, srgb: components)
  }

  static func wide(
    _ colorSpace: ColorSpace, _ red: Double, _ green: Double, _ blue: Double,
    _ alpha: Double, _ srgbRed: Double, _ srgbGreen: Double, _ srgbBlue: Double
  ) -> Color {
    Color(
      colorSpace: colorSpace, components: Rgb(red: red, green: green, blue: blue),
      alpha: alpha, srgb: Rgb(red: srgbRed, green: srgbGreen, blue: srgbBlue))
  }

  static func bezier(_ x1: Double, _ y1: Double, _ x2: Double, _ y2: Double)
    -> CubicBezier
  {
    CubicBezier(x1: x1, y1: y1, x2: x2, y2: y2)
  }

  static func layer(
    _ color: Color, _ offsetX: Double, _ offsetY: Double, _ blur: Double, _ spread: Double,
    _ inset: Bool
  ) -> ShadowLayer {
    ShadowLayer(
      color: color, offsetX: offsetX, offsetY: offsetY, blur: blur, spread: spread,
      inset: inset)
  }

  private enum Defaults {
    static let alumbra_token_color_saccent_splayed: [Color] = [
        srgb(0.055, 0.647, 0.914),
      ]
    static let alumbra_token_color_sborder_ssubtle: [Color] = [
        srgb(0.392, 0.455, 0.545),
        srgb(1.0, 1.0, 1.0),
        srgb(0.278, 0.333, 0.412),
        srgb(0.0, 0.0, 0.0),
      ]
    static let alumbra_token_color_scontrol_sbackground: [Color] = [
        srgb(0.118, 0.161, 0.231),
        srgb(0.0, 0.0, 0.0),
        srgb(0.886, 0.91, 0.941),
        srgb(1.0, 1.0, 1.0),
      ]
    static let alumbra_token_color_scontrol_sdisabled: [Color] = [
        srgb(0.118, 0.161, 0.231),
        srgb(0.0, 0.0, 0.0),
        srgb(0.945, 0.961, 0.976),
        srgb(1.0, 1.0, 1.0),
      ]
    static let alumbra_token_color_scontrol_sforeground: [Color] = [
        srgb(1.0, 1.0, 1.0),
        srgb(1.0, 1.0, 1.0),
        srgb(0.059, 0.09, 0.165),
        srgb(0.0, 0.0, 0.0),
      ]
    static let alumbra_token_color_scontrol_shover: [Color] = [
        srgb(0.2, 0.255, 0.333),
        srgb(0.059, 0.09, 0.165),
        srgb(0.796, 0.835, 0.882),
        srgb(0.886, 0.91, 0.941),
      ]
    static let alumbra_token_color_scontrol_spressed: [Color] = [
        srgb(0.278, 0.333, 0.412),
        srgb(0.118, 0.161, 0.231),
        srgb(0.58, 0.639, 0.722),
        srgb(0.796, 0.835, 0.882),
      ]
    static let alumbra_token_color_sfocus_sring: [Color] = [
        srgb(0.055, 0.647, 0.914),
        srgb(1.0, 1.0, 0.0),
        srgb(0.012, 0.412, 0.631),
        srgb(0.0, 0.0, 0.0),
      ]
    static let alumbra_token_color_ssurface_sbase: [Color] = [
        srgb(0.035, 0.055, 0.094),
        srgb(0.0, 0.0, 0.0),
        srgb(1.0, 1.0, 1.0),
        srgb(1.0, 1.0, 1.0),
      ]
    static let alumbra_token_color_ssurface_selevated: [Color] = [
        srgb(0.059, 0.09, 0.165),
        srgb(0.0, 0.0, 0.0),
        srgb(0.973, 0.98, 0.988),
        srgb(1.0, 1.0, 1.0),
      ]
    static let alumbra_token_color_ssurface_soverlay: [Color] = [
        srgb(0.118, 0.161, 0.231),
        srgb(0.0, 0.0, 0.0),
        srgb(0.945, 0.961, 0.976),
        srgb(1.0, 1.0, 1.0),
      ]
    static let alumbra_token_color_ssurface_sscrim: [Color] = [
        srgb(0.0, 0.0, 0.0, 0.72),
      ]
    static let alumbra_token_color_stext_sdisabled: [Color] = [
        srgb(0.392, 0.455, 0.545),
      ]
    static let alumbra_token_color_stext_sprimary: [Color] = [
        srgb(0.973, 0.98, 0.988),
        srgb(1.0, 1.0, 1.0),
        srgb(0.059, 0.09, 0.165),
        srgb(0.0, 0.0, 0.0),
      ]
    static let alumbra_token_color_stext_ssecondary: [Color] = [
        srgb(0.796, 0.835, 0.882),
        srgb(1.0, 1.0, 1.0),
        srgb(0.2, 0.255, 0.333),
        srgb(0.0, 0.0, 0.0),
      ]
    static let alumbra_token_elevation_smodal: [[ShadowLayer]] = [
        [layer(srgb(0.0, 0.0, 0.0), 0.0, 8.0, 24.0, 0.0, false)],
      ]
    static let alumbra_token_elevation_spanel: [[ShadowLayer]] = [
        [layer(srgb(0.0, 0.0, 0.0), 0.0, 4.0, 12.0, 0.0, false)],
      ]
    static let alumbra_token_elevation_sraised: [[ShadowLayer]] = [
        [layer(srgb(0.0, 0.0, 0.0), 0.0, 2.0, 6.0, 0.0, false)],
      ]
    static let alumbra_token_layer_sad_uchrome: [Double] = [
        30.0,
      ]
    static let alumbra_token_layer_sad_usdk_ucontainer: [Double] = [
        40.0,
      ]
    static let alumbra_token_layer_scaptions: [Double] = [
        10.0,
      ]
    static let alumbra_token_layer_scontrols: [Double] = [
        20.0,
      ]
    static let alumbra_token_layer_svideo_usurface: [Double] = [
        0.0,
      ]
    static let alumbra_token_luminance_shdr_uhighlight_uceiling: [Double] = [
        203.0,
        600.0,
      ]
    static let alumbra_token_luminance_sreference_uwhite: [Double] = [
        203.0,
      ]
    static let alumbra_token_motion_sduration_sinstant: [Double] = [
        0.0,
      ]
    static let alumbra_token_motion_sduration_slong: [Double] = [
        320.0,
        0.0,
      ]
    static let alumbra_token_motion_sduration_smedium: [Double] = [
        200.0,
        0.0,
      ]
    static let alumbra_token_motion_sduration_sshort: [Double] = [
        120.0,
        0.0,
      ]
    static let alumbra_token_motion_seasing_semphasized: [CubicBezier] = [
        bezier(0.2, 0.0, 0.0, 1.4),
      ]
    static let alumbra_token_motion_seasing_sstandard: [CubicBezier] = [
        bezier(0.2, 0.0, 0.0, 1.0),
      ]
    static let alumbra_token_opacity_sdisabled: [Double] = [
        0.48,
      ]
    static let alumbra_token_radius_slarge: [Double] = [
        16.0,
      ]
    static let alumbra_token_radius_smedium: [Double] = [
        8.0,
      ]
    static let alumbra_token_radius_spill: [Double] = [
        999.0,
      ]
    static let alumbra_token_radius_ssmall: [Double] = [
        4.0,
      ]
    static let alumbra_token_spacing_sextra_ularge: [Double] = [
        28.0,
        36.0,
        32.0,
        56.0,
      ]
    static let alumbra_token_spacing_slarge: [Double] = [
        20.0,
        28.0,
        24.0,
        40.0,
      ]
    static let alumbra_token_spacing_smedium: [Double] = [
        12.0,
        20.0,
        16.0,
        28.0,
      ]
    static let alumbra_token_spacing_snone: [Double] = [
        0.0,
      ]
    static let alumbra_token_spacing_ssmall: [Double] = [
        8.0,
        8.0,
        8.0,
        16.0,
      ]
    static let alumbra_token_spacing_stight: [Double] = [
        4.0,
        4.0,
        4.0,
        8.0,
      ]
    static let alumbra_token_target_sminimum: [Double] = [
        48.0,
        48.0,
        44.0,
        72.0,
      ]
    static let alumbra_token_typography_sfont_ufamily_sui: [[String]] = [
        ["system-ui"],
      ]
    static let alumbra_token_typography_sfont_usize_sbody: [Double] = [
        17.0,
        18.0,
        16.0,
        24.0,
      ]
    static let alumbra_token_typography_sfont_usize_scaption: [Double] = [
        13.0,
        14.0,
        12.0,
        18.0,
      ]
    static let alumbra_token_typography_sfont_usize_sdisplay: [Double] = [
        34.0,
        36.0,
        32.0,
        52.0,
      ]
    static let alumbra_token_typography_sfont_usize_stitle: [Double] = [
        25.0,
        26.0,
        24.0,
        36.0,
      ]
    static let alumbra_token_typography_sfont_uweight_sbold: [Double] = [
        700.0,
      ]
    static let alumbra_token_typography_sfont_uweight_smedium: [Double] = [
        500.0,
      ]
    static let alumbra_token_typography_sfont_uweight_sregular: [Double] = [
        400.0,
      ]
    static let alumbra_token_typography_sline_uheight_sbody: [Double] = [
        1.5,
      ]
    static let alumbra_token_typography_sline_uheight_scompact: [Double] = [
        1.2,
      ]
  }
}

#if canImport(SwiftUI)
  extension DesignTokens.Color {
    public var swiftUIColor: SwiftUI.Color {
      switch colorSpace {
      case .srgb:
        SwiftUI.Color(
          .sRGB, red: components.red, green: components.green, blue: components.blue,
          opacity: alpha)
      case .displayP3:
        SwiftUI.Color(
          .displayP3, red: components.red, green: components.green, blue: components.blue,
          opacity: alpha)
      case .rec2020:
        SwiftUI.Color(.sRGB, red: srgb.red, green: srgb.green, blue: srgb.blue, opacity: alpha)
      }
    }
  }
#endif
