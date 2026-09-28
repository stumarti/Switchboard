// ===========================================================================
// Switchboard — firmware for the Xteink X4 Pro smart-home remote.
//
// Built on the FreeInk SDK HAL (EInkDisplay / InputManager / BoardConfig),
// selected for the X4 Pro by -DFREEINK_DEVICE_X4PRO in platformio.ini.
//
// --- File layout -----------------------------------------------------------
// include/screen_*.h   one per screen: draw() to render it, enter() to switch
//                      to it, and (for carousel pages) handleTap()/handleDrag()
//                      for input and kick()/fetch() for live HA data.
// include/app/*.h      the cross-cutting layer:
//   rtc_state.h          the one record that survives deep sleep
//   input.h              the input task + one InFrame per loop() tick
//   frontlight.h         lamp levels and their trip through deep sleep
//   wifi_link.h          non-blocking Wi-Fi join / give-up / idle radio-off
//   net.h                the network worker: every command + the refresh
//   carousel.h           paging, drawing and sleeping the resting screens
//   quick_access.h       the Home-tap jump list / Quick Access hub
//   data_refresh.h       the shared server + Home Assistant pull
//   power.h              sleepFor() / shutdown / restart — every sleep path
//   stages.h             what one loop() tick does on each screen
//   boot.h               setup(): wake cause + sleep reason -> boot path
//
// Everything is header-only in a single translation unit, so include order
// matters: each header is included after the ones it depends on, and the
// few calls that point "backwards" go through screen_fwd.h.
// ===========================================================================

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <FrontlightManager.h>
#include <InputManager.h>

#include "config.h"
#include "globals_client.h"
#include "device_config_client.h"
#include "room_list_client.h"
#include "ha_client.h"
#include "persist.h"
#include "pairing_client.h"
#include "theme_client.h"
#include "mdi_icon.h"

#include "screen_common.h"
#include "screen_fwd.h"

// Shared state the screens build on — before any screen_*.h.
#include "app/rtc_state.h"
#include "app/input.h"
#include "app/frontlight.h"  // screen_shade.h drives the lamp levels
#include "app/wifi_link.h"

#include "screen_wifi.h"
#include "screen_pairing.h"
#include "screen_settings_info.h"
#include "screen_room_pick.h"
#include "screen_developer.h"
#include "screen_timeouts.h"
#include "screen_settings.h"
#include "screen_shade.h"
#include "screen_status.h"
#include "screen_climate.h"
#include "screen_lighting.h"
#include "screen_blinds.h"
#include "screen_music.h"
#include "screen_tv.h"
#include "screen_xbox.h"
#include "screen_receiver.h"
#include "screen_wifi_networks.h"
#include "screen_debug.h"
#include "screen_error.h"
#include "screen_splash.h"
#include "screen_power.h"

#include "app/carousel.h"
#include "app/quick_access.h"
#include "app/power.h"
#include "app/data_refresh.h"
#include "app/stages.h"
#include "app/boot.h"

void setup() { boot::run(); }

void loop() {
  const InFrame in = readInputFrame();
  pollBattery();     // self-throttled to ~1.5 s
  wifilink::poll();  // advance the background join / give-up
  tickStage(in);
  powerDownIdleRadio();  // Wi-Fi off once nothing has needed it for a while
  delay(5);
}
