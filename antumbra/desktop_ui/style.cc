#include "alumbra/desktop_ui/style.h"

#include <chrono>
#include <cstdint>

#include "alumbra/design_tokens/design_tokens.h"
#include "alumbra/desktop_ui/geometry.h"

namespace alumbra::desktop_ui {
namespace {

std::int64_t Microseconds(design_tokens::Milliseconds duration) {
  return std::chrono::duration_cast<std::chrono::microseconds>(duration)
      .count();
}

}

Style StyleOf(const design_tokens::Tokens& tokens) {
  Style style;
  style.tight = static_cast<float>(tokens.alumbra_token_spacing_stight);
  style.small = static_cast<float>(tokens.alumbra_token_spacing_ssmall);
  style.medium = static_cast<float>(tokens.alumbra_token_spacing_smedium);
  style.large = static_cast<float>(tokens.alumbra_token_spacing_slarge);
  style.extra_large =
      static_cast<float>(tokens.alumbra_token_spacing_sextra_ularge);
  style.target = static_cast<float>(tokens.alumbra_token_target_sminimum);
  style.radius_small = static_cast<float>(tokens.alumbra_token_radius_ssmall);
  style.radius_medium = static_cast<float>(tokens.alumbra_token_radius_smedium);
  style.radius_large = static_cast<float>(tokens.alumbra_token_radius_slarge);
  style.radius_pill = static_cast<float>(tokens.alumbra_token_radius_spill);
  style.button_radius =
      static_cast<float>(tokens.alumbra_token_player_sbutton_sradius);
  style.font_body =
      static_cast<float>(tokens.alumbra_token_typography_sfont_usize_sbody);
  style.font_title =
      static_cast<float>(tokens.alumbra_token_typography_sfont_usize_stitle);
  style.font_caption =
      static_cast<float>(tokens.alumbra_token_typography_sfont_usize_scaption);
  style.disabled_opacity =
      static_cast<float>(tokens.alumbra_token_opacity_sdisabled);
  style.scrim = FromToken(tokens.alumbra_token_color_ssurface_sscrim);
  style.elevated = FromToken(tokens.alumbra_token_color_ssurface_selevated);
  style.border = FromToken(tokens.alumbra_token_color_sborder_ssubtle);
  style.text_primary = FromToken(tokens.alumbra_token_color_stext_sprimary);
  style.text_secondary = FromToken(tokens.alumbra_token_color_stext_ssecondary);
  style.accent = FromToken(tokens.alumbra_token_color_saccent_splayed);
  style.focus_ring = FromToken(tokens.alumbra_token_player_sfocus_sring);
  style.track = FromToken(tokens.alumbra_token_player_sseekbar_strack);
  style.played = FromToken(tokens.alumbra_token_player_sseekbar_splayed);
  style.buffered = FromToken(tokens.alumbra_token_color_stext_ssecondary);
  style.idle.background =
      FromToken(tokens.alumbra_token_player_sbutton_sbackground);
  style.idle.foreground =
      FromToken(tokens.alumbra_token_player_sbutton_sforeground);
  style.hover.background =
      FromToken(tokens.alumbra_token_player_sbutton_sbackground_shover);
  style.hover.foreground =
      FromToken(tokens.alumbra_token_player_sbutton_sforeground_shover);
  style.pressed.background =
      FromToken(tokens.alumbra_token_player_sbutton_sbackground_spressed);
  style.pressed.foreground =
      FromToken(tokens.alumbra_token_player_sbutton_sforeground_spressed);
  style.focus.background =
      FromToken(tokens.alumbra_token_player_sbutton_sbackground_sfocus);
  style.focus.foreground =
      FromToken(tokens.alumbra_token_player_sbutton_sforeground_sfocus);
  style.disabled.background =
      FromToken(tokens.alumbra_token_player_sbutton_sbackground_sdisabled);
  style.disabled.foreground =
      FromToken(tokens.alumbra_token_player_sbutton_sforeground_sdisabled);
  style.fade_us = Microseconds(tokens.alumbra_token_motion_sduration_smedium);
  style.spinner_period_us =
      Microseconds(tokens.alumbra_token_motion_sduration_slong) * 3;
  style.easing = tokens.alumbra_token_motion_seasing_sstandard;
  return style;
}

}
