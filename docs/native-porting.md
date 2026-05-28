# Native Porting

This document records the native macOS/Linux migration contract. `Native-Port-Plan.md`
is still the task-by-task migration source of truth; this page is the durable contributor
reference for scope, commands, feature disposition, and dependency decisions.

## Product Contract

Native WLED keeps the WLED product name and preserves the public API behavior that
integrations use. The native runtime targets macOS and Linux, runs as a headless local
web service by default, and serves the existing browser UI from the process once the
server exists.

ESP32 and ESP8266 support are intentionally removed from the native product line. This
is a user-visible breaking change for firmware users: native WLED does not drive ESP
GPIO, RMT, I2S, LEDC, hardware IR receivers, hardware DMX UART, ESP Ethernet boards,
ESP watchdog/brownout handling, or OTA firmware update paths.

The first runnable native experience is a command-line executable named `wled-native`.
It now starts a native runtime loop, logs startup/shutdown state to stdout, exits cleanly
on SIGINT/SIGTERM, persists a generated MAC-compatible identity in the native config
root, serves the existing `wled00/data` web assets through a native localhost HTTP
server, applies core JSON/WebSocket state changes, runs native effect/palette,
preset, playlist, and nightlight logic, and publishes a memory-backed render buffer
through a null output backend.

## Native Developer Commands

Run the standard web checks first when validating the transition:

```sh
npm test
npm run build
```

Build and test the native runtime:

```sh
scripts/native-build.sh
scripts/native-test.sh
scripts/native-run.sh --help
```

Equivalent npm aliases are available:

```sh
npm run native:build
npm run native:test
npm run native:run -- --help
```

Native build output goes under `build/native/` and is not committed.

Native network tests open localhost UDP sockets. In sandboxed agent environments,
`scripts/native-test.sh` may need explicit permission to bind local sockets.

## Native Runtime And Config

The native runtime resolves its config root in this order:

1. `--config-dir <path>`
2. `WLED_CONFIG_DIR=<path>`
3. macOS: `~/Library/Application Support/WLED`
4. Linux: `$XDG_CONFIG_HOME/wled`, or `~/.config/wled` when `XDG_CONFIG_HOME` is unset
5. Fallback: `./.wled-native`

At startup, the runtime creates or loads `native-id.json` in that root. The file stores
a lowercase 12-hex-digit MAC-compatible identifier beginning with `02`, marking it as
locally administered. This value is logged as `nativeMac` now and must later be exposed
consistently through Zeroconf TXT records and `/json/info.mac`.

The runtime returns exit code `0` for clean shutdown and `75` for a native restart
request. The restart code is an internal contract for later service wrappers; the
current CLI does not restart itself.

## Native HTTP And WebSocket Layer

The first native HTTP adapter is dependency-free and intentionally narrow. It binds to
`127.0.0.1:8080` by default, or to `--host` and `--port` when specified. LAN exposure
therefore requires an explicit host such as `--host 0.0.0.0`.

The adapter currently serves:

- Existing browser assets from `wled00/data`, including `/`, `/settings`, `/edit`,
  `/liveview`, `/liveview2D`, static JS/CSS/image routes, and 404 handling.
- `/json`, `/json/state`, `/json/info`, `/json/si`, `/version`, `/uptime`, and
  `/freeheap` responses backed by the native core state.
- Effect, palette, palette-preview, and settings-script compatibility routes:
  `/json/effects`, `/json/fxdata`, `/json/palettes`, `/json/palx`, and
  `/settings/s.js`.
- `/ws` WebSocket handshakes, initial state/info delivery, `"p"` heartbeat replies,
  JSON state updates for core state, segment effect/palette/color controls, verbose
  `{"v":true}` state/info responses, and `{"lv":true}` live-view frames sourced from
  the normal native render buffer.
- `/presets.json` from the native config root. Missing preset storage is initialized
  as `{"0":{}}`, and JSON state with `ps`, `psave`, or `playlist` is handled by the
  native core.

Firmware OTA and bootloader update routes are not supported by native WLED. `/update`
returns `410 Gone`, and `/upload` returns `501 Not Implemented` until file-editor upload
semantics are deliberately ported.

The adapter does not yet reuse `wled00/wled_server.cpp`, `wled00/json.cpp`, or
`wled00/ws.cpp`. It is a host boundary that keeps browser/API bring-up testable while
later tasks remove ESP-only coupling from the existing handlers.

## Native Core State And Effects

`native/core/NativeWledCore.*` is the first native implementation of the WLED state
value path. It preserves the public JSON shape for `on`, `bri`, `transition`, `ps`,
`pl`, `nl`, and segment fields such as `start`, `stop`, `fx`, `sx`, `ix`, `pal`, and
`col`.

The core exposes WLED-style effect and palette metadata and renders every selected
effect ID to the internal render buffer. A focused set of host-portable 1D behaviors is
implemented directly now, including solid, blink/strobe, breathe/fade, rainbow,
theater/chase, sparkle/glitter, gradient/palette, meteor, and sinelon-style effects.
Other effect IDs use a deterministic palette fallback so JSON/API and browser controls
remain functional until the full `wled00/FX*.cpp` engine is separated from ESP-only
dependencies. Audioreactive IDs also fall back to deterministic rendering when no
microphone data is available.

Presets are read from and saved to `presets.json` under the native config root. Playlist
objects with `ps`, `dur`, `transition`, `repeat`, `end`, and `r` are supported for
native preset cycling. Nightlight state is represented in JSON and fades brightness
toward `tbri`; native time is currently monotonic process time rather than a complete
NTP/timezone scheduler.

## Native Render Buffer And Output

The first output backend is a memory render buffer plus a null backend. The render
buffer stores RGBW pixels and CCT metadata, can encode common RGB/GRB/RGBW orders,
estimates current at 20 mA per fully-on channel, receives frames from the native core,
and supplies live-view data to the native WebSocket adapter. The null backend records
the last frame and frame count for tests, but it intentionally does not drive physical
LEDs.

Host-native physical output remains future work. USB serial, USB DMX, SPI, HID, vendor
SDKs, or network forwarding backends should attach behind the narrow native output
interface and must avoid blocking the render loop.

## Native Network Layer

The first native network adapter is intentionally small:

- DNS lookup uses `getaddrinfo` for IPv4 host resolution.
- Interface discovery uses `getifaddrs` and records IPv4 address, interface name,
  loopback state, and up/down state.
- UDP loopback send/receive is wrapped in `NativeUdpSocket`.
- Zeroconf/mDNS advertisement is not implemented yet. It remains required for Home
  Assistant discovery and must be added behind a dedicated adapter after the dependency
  choice is documented.

## Feature Matrix

| Feature area | Native disposition | Notes |
|---|---|---|
| WLED product name | Keep | Preserve user-visible name and integration expectations. |
| JSON API (`/json`, `/json/state`, `/json/info`, `/json/si`) | Keep | API compatibility is a release gate. |
| WebSocket state updates (`/ws`) | Keep | Preserve state push behavior and existing live-view messages. |
| Web UI | Keep | Serve existing `wled00/data` UI through the native server. |
| Effects, palettes, segments, presets, playlists | Keep | Native core state now backs JSON/WebSocket and the internal render buffer; full legacy FX engine parity remains in progress. |
| Home Assistant discovery and identity | Keep | First useful native milestone release gate; persist a MAC-compatible instance ID. |
| Config files (`cfg.json`, `wsec.json`, `presets.json`, palettes, ledmaps) | Replace storage backend | Keep file names and JSON semantics under the native config root. |
| First-run config | Replace initialization | Use WLED defaults; do not auto-import ESP exports. |
| LED output on first milestone | Replace with null backend | Internal RGBW/CCT render buffer plus no-op/null output is active. |
| Additional browser preview | Remove from scope | Keep only existing peek/live-view behavior backed by the render buffer. |
| ESP GPIO/RMT/I2S/LEDC LED drivers | Remove | Native host process will not drive ESP hardware paths. |
| Hardware IR receivers | Remove | Hardware receiver support is ESP-specific. |
| Hardware DMX GPIO/UART | Remove initially | Later native USB DMX or network backends may be added explicitly. |
| OTA firmware update paths | Remove | Native updates should use host packaging/service mechanisms later. |
| ESP-NOW | Remove | ESP-only radio protocol. |
| WiFi/AP provisioning | Remove | Native hosts use OS networking. |
| UDP realtime, Art-Net, DDP, E1.31 inputs | Keep/native-backend | These are host-relevant IP protocols. |
| MQTT | Keep/native-backend | Preserve behavior behind a native adapter. |
| Alexa/Hue-compatible behavior | Keep where practical | Preserve network integration behavior if dependencies remain reasonable. |
| Native microphone/audio reactive input | Native-backend | Use OS default input first; device selection later. |
| Usermod harness | Keep | Existing data contracts remain valuable, especially audioreactive. |
| PlatformIO firmware builds during transition | Keep temporarily | Do not remove until native build, tests, and CI can replace firmware validation. |

## Native CLI Placeholders

The runtime executable accepts these options now so scripts and docs stabilize early:

- `--help`
- `--version`
- `--config-dir <path>`
- `--host <address>`
- `--port <port>`
- `--log-level <level>`
- `--duration-ms <ms>`
- `--max-loops <count>`

`--duration-ms` and `--max-loops` are development/test controls for exercising the
runtime loop without leaving a long-running process active.

## Dependency Manifest

No third-party native runtime library is introduced by the skeleton or the first
HTTP/WebSocket/output slice. The current native build uses only the C++ standard library,
POSIX host APIs, and CMake/CTest as build tooling.

Candidate future dependencies must be recorded here before use:

| Dependency | License | Source/version | Packaging impact | Adapter boundary |
|---|---|---|---|---|
| Boost.Beast | Boost Software License 1.0 | Not selected | Would require Boost packages or vendoring policy | HTTP/WebSocket adapter only |
| libwebsockets | MIT | Not selected | Would require native system package or vendored build | HTTP/WebSocket adapter only |
| PortAudio | MIT-style PortAudio license | Not selected | Would require host audio package and runtime device access | Native audio capture adapter only |

Do not couple WLED core files directly to a chosen HTTP, WebSocket, MQTT, audio, or
Zeroconf library. Keep native dependencies behind narrow adapter boundaries.
