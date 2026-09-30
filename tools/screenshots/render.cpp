// The remote's real screens, drawn on a PC: the firmware (src/main.cpp, with
// hardware stand-ins from stubs/) talks to a running Switchboard Server over
// HTTP exactly as a remote would, then draws each screen into its
// framebuffer, saved as a 480x800 PNG. See run.sh.
//
//   render <out-dir>    SB_MAC picks the demo remote (default the Sofa remote,
//                       a0:b1:c2:00:00:02), SB_BATTERY its battery % (64)

#include "../../src/main.cpp"

#include <string>
#include <vector>
#include <zlib.h>

static std::string g_out;

// The panel is 800x480 landscape; the remote draws portrait (90° CW), so
// logical (x, y) is panel (y, 479 - x). Set bit = white. Saved as a 1-bit
// greyscale PNG.
static void be32(std::vector<uint8_t>& v, uint32_t x) {
  for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<uint8_t>(x >> s));
}
static void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> c;
  be32(c, static_cast<uint32_t>(data.size()));
  c.insert(c.end(), type, type + 4);
  c.insert(c.end(), data.begin(), data.end());
  be32(c, static_cast<uint32_t>(crc32(0, c.data() + 4, static_cast<uInt>(c.size() - 4))));
  fwrite(c.data(), 1, c.size(), f);
}
static void save(const char* name) {
  const uint8_t* fb = ui.display().getFrameBuffer();
  const int rowBytes = Ui::W / 8;
  std::vector<uint8_t> raw;
  for (int ly = 0; ly < Ui::H; ++ly) {
    raw.push_back(0);  // filter: none
    for (int bx = 0; bx < rowBytes; ++bx) {
      uint8_t out = 0;
      for (int b = 0; b < 8; ++b) {
        const int lx = bx * 8 + b, px = ly, py = Ui::W - 1 - lx;
        if (fb[py * 100 + (px >> 3)] & (0x80 >> (px & 7))) out |= static_cast<uint8_t>(0x80 >> b);
      }
      raw.push_back(out);
    }
  }
  uLongf zlen = compressBound(static_cast<uLong>(raw.size()));
  std::vector<uint8_t> z(zlen);
  compress2(z.data(), &zlen, raw.data(), static_cast<uLong>(raw.size()), 9);
  z.resize(zlen);
  std::vector<uint8_t> ihdr;
  be32(ihdr, Ui::W);
  be32(ihdr, Ui::H);
  ihdr.insert(ihdr.end(), {1, 0, 0, 0, 0});  // 1 bit, greyscale
  const std::string path = g_out + "/" + name + ".png";
  FILE* f = fopen(path.c_str(), "wb");
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  fwrite(sig, 1, 8, f);
  chunk(f, "IHDR", ihdr);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  fclose(f);
  printf("  %s\n", name);
}

static void drainNetwork() {
  for (int i = 0; i < 4; ++i) {
    net::serviceCommands();
    delay(5000);  // past the readback quiet period
    net::serviceReadbacks();
  }
}

static void page(uint8_t p, const char* name, int pressed = -1) {
  carouselPage = p;
  drawStandbyContent(false, pressed);
  save(name);
}

int main(int argc, char** argv) {
  g_out = argc > 1 ? argv[1] : ".";
  ui.begin();
  ui.rebindFonts();
  wifilink::state = wifilink::State::Up;

  // Pair (the demo server has this MAC approved) and pull everything a wake does.
  pairing::ensureMac();
  if (!pairing::registerOnce() || !pairing::paired) {
    fprintf(stderr, "couldn't pair with the server: %s\n", pairing::status);
    return 1;
  }
  deviceconfig::applyServerAssignedSlug(pairing::assignedSlug);
  // The network worker never runs here: stand in for it, so the art
  // fetches the refresh queues (album, box art) run before drawing.
  net::g_task = reinterpret_cast<TaskHandle_t>(1);
  if (!refreshStandby()) fprintf(stderr, "the refresh didn't bring the weather\n");
  if (!deviceconfig::ok) {
    fprintf(stderr, "no room config for %s\n", deviceconfig::activeSlug);
    return 1;
  }
  screen_xbox::loadVisibleArt();
  drainNetwork();
  pollBattery();
  roomlist::fetch();

  // The carousel.
  page(0, "status");
  page(kPageLighting, "lighting");
  screen_lighting::tab = 1;
  page(kPageLighting, "lighting-lights");
  screen_lighting::tab = 2;
  page(kPageLighting, "lighting-colour");
  screen_lighting::tab = 0;
  page(kPageBlinds, "blinds");
  page(kPageMusic, "music");
  page(kPageTv, "tv");
  page(kPageXbox, "xbox");
  page(kPageWifi, "wifi");
  page(kPageClimate, "climate");
  page(kPageReceiver, "receiver");
  screen_wifi_networks::g_qrIdx = 1;
  screen_wifi_networks::drawQr();
  save("wifi-qr");

  // Home tap: the Quick Access hub, then the built-in jump list.
  carouselPage = 0;
  openQuickAccess();
  save("quick-access");
  const int hubItems = deviceconfig::hubItemCount;
  deviceconfig::hubItemCount = 0;
  openQuickAccess();
  save("jump-list");
  deviceconfig::hubItemCount = hubItems;
  jumpOpen = false;

  // Hold Home: the control shade over the page.
  carouselPage = 0;
  drawStandbyContent(false, -1);
  frontlight.setBrightness(60);
  screen_shade::open = true;
  screen_shade::draw();
  save("shade");
  screen_shade::open = false;

  // Settings and its screens.
  screen_settings::enter();
  save("settings");
  screen_settings_info::enter();
  save("device-info");
  screen_room_pick::enter();
  save("select-room");
  screen_timeouts::enter();
  save("timeouts");
  screen_developer::enter();
  save("developer");

  // Updates.
  snprintf(screen_ota::offer.version, sizeof(screen_ota::offer.version), "v1.4.0");
  screen_ota::offer.size = 1482752;
  screen_ota::phase = screen_ota::Phase::Offer;
  screen_ota::draw();
  save("update-offer");
  screen_ota::phase = screen_ota::Phase::Countdown;
  screen_ota::g_unattended = true;
  screen_ota::g_countdown = 7;
  screen_ota::draw();
  save("update-countdown");
  screen_ota::phase = screen_ota::Phase::Installing;
  screen_ota::g_startMs = millis() - 38000;
  snprintf(screen_ota::g_startedAt, sizeof(screen_ota::g_startedAt), "03:12");
  screen_ota::g_pct = 45;
  screen_ota::draw();
  save("update-installing");
  screen_ota::phase = screen_ota::Phase::UpToDate;
  screen_ota::draw();
  save("update-none");

  // Pairing and the error screens.
  screen_pairing::draw(screen_pairing::View::Pending);
  save("pairing");
  screen_no_ha::draw();
  save("error-server");
  screen_no_room::draw();
  save("error-no-room");
  g_battPct = 4;
  screen_low_battery::enter();
  save("low-battery");
  screen_splash::draw();
  save("splash");
  return 0;
}
