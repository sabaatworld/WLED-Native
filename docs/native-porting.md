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
on SIGINT/SIGTERM, and persists a generated MAC-compatible identity in the native config
root. Later tasks will attach the web server, JSON API, WebSocket updates, and output
backends.

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
| Effects, palettes, segments, presets, playlists | Keep | Back with an internal render buffer before physical output exists. |
| Home Assistant discovery and identity | Keep | First useful native milestone release gate; persist a MAC-compatible instance ID. |
| Config files (`cfg.json`, `wsec.json`, `presets.json`, palettes, ledmaps) | Replace storage backend | Keep file names and JSON semantics under the native config root. |
| First-run config | Replace initialization | Use WLED defaults; do not auto-import ESP exports. |
| LED output on first milestone | Replace with null backend | Internal render buffer plus no-op/null output comes first. |
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

`--host` and `--port` remain server placeholders until the HTTP task. `--duration-ms`
and `--max-loops` are development/test controls for exercising the runtime loop without
leaving a long-running process active.

## Dependency Manifest

No third-party native runtime library is introduced by the skeleton. The current native
build uses only the C++ standard library plus CMake/CTest as build tooling.

Candidate future dependencies must be recorded here before use:

| Dependency | License | Source/version | Packaging impact | Adapter boundary |
|---|---|---|---|---|
| Boost.Beast | Boost Software License 1.0 | Not selected | Would require Boost packages or vendoring policy | HTTP/WebSocket adapter only |
| libwebsockets | MIT | Not selected | Would require native system package or vendored build | HTTP/WebSocket adapter only |
| PortAudio | MIT-style PortAudio license | Not selected | Would require host audio package and runtime device access | Native audio capture adapter only |

Do not couple WLED core files directly to a chosen HTTP, WebSocket, MQTT, audio, or
Zeroconf library. Keep native dependencies behind narrow adapter boundaries.
