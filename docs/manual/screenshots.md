# Screenshots

Every screenshot in this manual is made from the real code, against the [demo](demo.md), so they can be made again whenever a screen changes.

## The remote

`tools/screenshots/` in the firmware repository compiles the firmware's own drawing code for your PC, with small stand-ins for the hardware (the display is just a framebuffer, the buttons never press). It then pairs with a running server exactly as a remote would, pulls its room and Home Assistant state, and draws each screen to a 480×800 PNG, one bit a pixel, as the panel shows it.

```sh
# In Switchboard-Server: the demo, on the port the firmware uses
DEMO_PORT=45678 node tools/demo/demo.js

# In Switchboard (with the FreeInk SDK beside it, as for a firmware build)
tools/screenshots/run.sh              # writes docs/manual/images/remote/
```

It needs `g++` and zlib. On first run it downloads ArduinoJson and Nayuki's QR code generator (which the Wi-Fi page's codes use). It checks in as the demo's Sofa remote; `SB_MAC` picks another, `SB_BATTERY` the battery it reports (64%) and `SCREENSHOT_VERSION` its firmware version (`v1.3.0`).

## The admin UI and viewports

`tools/demo/screenshots.js` in the server repository takes the admin pages, and every viewport screen at the panel's own 800×480, with Playwright.

```sh
# In Switchboard-Server, with the demo running on 45678 as above
node tools/demo/screenshots.js ../Switchboard/docs/manual/images
```

These viewport screens are the server's own preview of what a display would draw.

## The viewport's own renders

The images in [The viewport](viewport/screens.md) are drawn by the viewport firmware itself, built for the computer instead of the panel (in [Switchboard-Viewport](https://github.com/stumarti/Switchboard-Viewport)):

```sh
./test/host/run.sh        # every screen in test/fixtures -> test/host/out/
```

Copy them to `images/viewport/device/` (`kitchen-panel--status.png` becomes `kitchen-panel-status.png`). The fixtures are the demo's screen states, as the server sends them (`test/fixtures/*.json`), and the per-item icons they use (`test/fixtures/icons/`).
