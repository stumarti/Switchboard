#pragma once

// ===========================================================================
// default_icon_table — every icon this firmware ships with, compiled in as
// the guaranteed fallback (and the only source, until an admin publishes a
// custom theme from Switchboard-Server's Theme page - see icons.h). Pure
// data: {name, &compile-time Icon} pairs, one per existing weather_icons.h/
// assets.h constant, keyed by name so icons::get() can look any of them up
// by string instead of every call site referencing a C identifier directly.
//
// `name` is derived mechanically from each original identifier - strip the
// leading 'k', lowercase the rest (kWx_ui_cog -> "wx_ui_cog", kNav_status ->
// "nav_status", kLogoRemote -> "logoremote") - and matches Switchboard-
// Server's lib/assets/icon-slots.js `key` field exactly, so a compiled theme
// pack's slot names line up with these with no translation needed.
//
// MAINTENANCE: adding a new compile-time icon anywhere in weather_icons.h or
// assets.h means adding its entry here too (and to the server's icon-slots.js
// manifest, if it should be re-skinnable) - nothing enforces this
// automatically.
// ===========================================================================

#include "Icon.h"
#include "weather_icons.h"
#include "assets.h"

struct NamedIcon {
  const char* name;
  const freeink::Icon* icon;
};

static const NamedIcon kDefaultIcons[] = {
    {"wx_clear_night", &kWx_clear_night},
    {"wx_cloudy", &kWx_cloudy},
    {"wx_exceptional", &kWx_exceptional},
    {"wx_fog", &kWx_fog},
    {"wx_hail", &kWx_hail},
    {"wx_lightning", &kWx_lightning},
    {"wx_lightning_rainy", &kWx_lightning_rainy},
    {"wx_partlycloudy", &kWx_partlycloudy},
    {"wx_pouring", &kWx_pouring},
    {"wx_rainy", &kWx_rainy},
    {"wx_snowy", &kWx_snowy},
    {"wx_snowy_rainy", &kWx_snowy_rainy},
    {"wx_sunny", &kWx_sunny},
    {"wx_windy", &kWx_windy},
    {"wx_windy_variant", &kWx_windy_variant},
    {"wx_night_partlycloudy", &kWx_night_partlycloudy},
    {"wxf_clear_night", &kWxF_clear_night},
    {"wxf_cloudy", &kWxF_cloudy},
    {"wxf_exceptional", &kWxF_exceptional},
    {"wxf_fog", &kWxF_fog},
    {"wxf_hail", &kWxF_hail},
    {"wxf_lightning", &kWxF_lightning},
    {"wxf_lightning_rainy", &kWxF_lightning_rainy},
    {"wxf_partlycloudy", &kWxF_partlycloudy},
    {"wxf_pouring", &kWxF_pouring},
    {"wxf_rainy", &kWxF_rainy},
    {"wxf_snowy", &kWxF_snowy},
    {"wxf_snowy_rainy", &kWxF_snowy_rainy},
    {"wxf_sunny", &kWxF_sunny},
    {"wxf_windy", &kWxF_windy},
    {"wxf_windy_variant", &kWxF_windy_variant},
    {"wxf_night_partlycloudy", &kWxF_night_partlycloudy},
    {"wx_ui_wind", &kWx_ui_wind},
    {"wx_ui_humidity", &kWx_ui_humidity},
    {"wx_ui_indoor", &kWx_ui_indoor},
    {"wx_ui_air", &kWx_ui_air},
    {"wx_ui_light", &kWx_ui_light},
    {"wx_ui_bright", &kWx_ui_bright},
    {"wx_ui_dimmer", &kWx_ui_dimmer},
    {"wx_ui_brighter", &kWx_ui_brighter},
    {"wx_ui_warm", &kWx_ui_warm},
    {"wx_ui_refresh", &kWx_ui_refresh},
    {"wx_ui_cog", &kWx_ui_cog},
    {"wx_ui_chevron_up", &kWx_ui_chevron_up},
    {"wx_ui_temp_warm", &kWx_ui_temp_warm},
    {"wx_ui_temp_daylight", &kWx_ui_temp_daylight},
    {"wx_ui_temp_cool", &kWx_ui_temp_cool},
    {"wx_ui_room", &kWx_ui_room},
    {"wx_ui_info", &kWx_ui_info},
    {"wx_ui_wifi", &kWx_ui_wifi},
    {"wx_ui_restart", &kWx_ui_restart},
    {"wx_ui_back", &kWx_ui_back},
    {"wx_ui_bulb_on", &kWx_ui_bulb_on},
    {"wx_ui_bulb_off", &kWx_ui_bulb_off},
    {"wx_climate_off", &kWx_climate_off},
    {"wx_climate_heat", &kWx_climate_heat},
    {"wx_climate_cool", &kWx_climate_cool},
    {"wx_climate_auto", &kWx_climate_auto},
    {"wx_climate_fan", &kWx_climate_fan},
    {"wx_climate_thermo", &kWx_climate_thermo},
    {"wx_blinds_open", &kWx_blinds_open},
    {"wx_blinds_closed", &kWx_blinds_closed},
    {"wx_music_vol_on", &kWx_music_vol_on},
    {"wx_music_vol_off", &kWx_music_vol_off},
    {"wx_music_vol_minus", &kWx_music_vol_minus},
    {"wx_music_vol_plus", &kWx_music_vol_plus},
    {"wx_music_play", &kWx_music_play},
    {"wx_music_pause", &kWx_music_pause},
    {"wx_music_next", &kWx_music_next},
    {"wx_music_prev", &kWx_music_prev},
    {"wx_tv_app_netflix", &kWx_tv_app_netflix},
    {"wx_tv_app_youtube", &kWx_tv_app_youtube},
    {"wx_tv_app_generic", &kWx_tv_app_generic},
    {"wx_tv_back", &kWx_tv_back},
    {"wx_tv_home", &kWx_tv_home},
    {"wx_tv_power", &kWx_tv_power},
    {"wx_jump_status", &kWx_jump_status},
    {"wx_jump_lighting", &kWx_jump_lighting},
    {"wx_jump_blinds", &kWx_jump_blinds},
    {"wx_jump_music", &kWx_jump_music},
    {"wx_jump_tv", &kWx_jump_tv},
    {"wx_jump_xbox", &kWx_jump_xbox},
    {"wx_jump_climate", &kWx_jump_climate},
    {"wx_jump_wifi", &kWx_jump_wifi},
    {"wx_jump_settings", &kWx_jump_settings},
    {"wx_jump_selftest", &kWx_jump_selftest},
    {"wx_jump_errors", &kWx_jump_errors},
    {"wx_off_power", &kWx_off_power},
    {"wx_off_arrow", &kWx_off_arrow},
    {"logoremote", &kLogoRemote},
    {"wifiglyph", &kWifiGlyph},
    {"wifiradiating", &kWifiRadiating},
    {"wifiempty", &kWifiEmpty},
    {"moon", &kMoon},
    {"nav_status", &kNav_status},
    {"nav_climate", &kNav_climate},
    {"nav_music", &kNav_music},
    {"nav_tv", &kNav_tv},
    {"nav_xbox", &kNav_xbox},
    {"nav_lighting", &kNav_lighting},
    {"nav_wifi", &kNav_wifi},
    {"mode_heat", &kMode_heat},
    {"mode_cool", &kMode_cool},
    {"mode_off", &kMode_off},
    {"mode_auto", &kMode_auto},
    {"nav_blinds", &kNav_blinds},
    {"err_battery", &kErr_battery},
    {"err_cloud", &kErr_cloud},
    {"err_server", &kErr_server},
};

static constexpr int kDefaultIconCount = sizeof(kDefaultIcons) / sizeof(kDefaultIcons[0]);
