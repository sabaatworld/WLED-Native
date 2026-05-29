# WLED Native Port Plan

Goal: convert this repository from ESP32/ESP8266 firmware into a native macOS/Linux WLED application while preserving every WLED feature that can reasonably run on a host OS.

This plan is the single source of truth for native-port scope, status, and execution notes. Do **not** create `docs/native-porting.md`, `docs/native-running.md`, or any separate native-port status document. When a native-port task changes scope, discovers risks, or completes work, update the relevant section of this file in the same change.

## Non-Negotiable Direction

- Modify the original WLED implementation in `wled00/`. The migration target is the existing source tree, not a second product or a separate top-level native implementation. Do not recreate or grow a top-level `native/` product source tree; put WLED runtime/source changes in `wled00/` and use shared repository locations only for build scripts, tests, CI, or packaging glue.
- Use the existing `wled00-backup/` snapshot only as a reference for comparison and recovery while editing `wled00/`. Do not rebuild, refresh, compile, serve, test, package, or evolve `wled00-backup/` as product code, and do not allow it to become a parallel port.
- Prefer changing WLED source files where portability issues appear. Do not build a broad Arduino/ESP shim or API-emulation subsystem as the primary architecture. Small shared utilities are acceptable when they remove repetition or provide one host implementation for a concept used in many files, but those utilities should serve the original `wled00` code rather than hiding an alternate implementation.
- Preserve host-feasible WLED features by doing the required porting work in original source files. Remove ESP-only, GPIO-only, board-specific, or physical peripheral features as soon as they are identified; do not keep them as blockers, fallback paths, compatibility shells, stubs, or interim abstractions.
- Keep the user-visible product name **WLED**. Use "native" only to describe build/runtime internals, host-specific behavior, or command names.
- Preserve the existing browser UI, JSON API, WebSocket behavior, presets, playlists, segments, effects, palettes, realtime network protocols, MQTT, Hue/Alexa-compatible behavior where feasible, Home Assistant compatibility, and the usermod harness. Remove bundled usermods and source paths that depend on ESP-only hardware or GPIO/peripheral access; keep the ability to add new host-compatible usermods.
- Update existing tests and add basic new tests inside the repository's normal test flow. Do not create an isolated test suite that ignores WLED's current `npm test`, `tools/`, `test/`, web build, or future native CI entry points.
- Keep generated web headers (`wled00/html_*.h`, `wled00/js_*.h`) treated as generated artifacts. Do not hand-edit them.

## Research Baseline

- Home Assistant's current WLED integration is local-push, auto-discovers WLED through Zeroconf, exposes lights/selects/numbers/sensors/switches/buttons/update entities, prefers WebSocket push updates, falls back to polling, and relies on the WLED JSON API. Source: <https://www.home-assistant.io/integrations/wled/>.
- Home Assistant's current WLED manifest declares `zeroconf: ["_wled._tcp.local."]` and depends on the `wled` Python client. Source: <https://raw.githubusercontent.com/home-assistant/core/dev/homeassistant/components/wled/manifest.json>.
- Home Assistant's config flow uses the discovered Zeroconf `mac` TXT property when present and otherwise queries the device, then uses the WLED info MAC address as the unique ID. Native WLED therefore needs a stable MAC-compatible instance ID persisted in the config root and exposed consistently through discovery and `/json/info`. Source: <https://raw.githubusercontent.com/home-assistant/core/dev/homeassistant/components/wled/config_flow.py>.
- The WLED JSON API contract is centered on `/json`, `/json/state`, `/json/info`, `state`, `info`, `effects`, `palettes`, segments, presets, playlists, and partial state POSTs. Source: <https://kno.wled.ge/interfaces/json-api/>.
- The WLED WebSocket contract uses `/ws`, accepts JSON state updates, sends state/info updates on lighting changes, supports `{"v":true}`, and supports live LED value streaming with `{"lv":true}` for the existing Peek/live-view feature. Source: <https://kno.wled.ge/interfaces/websocket/>.
- WLED network realtime protocols remain relevant on native hosts because E1.31, Art-Net, DDP, and UDP sync are IP protocols, even though WiFi/AP-specific advice in current docs becomes irrelevant. Sources: <https://kno.wled.ge/interfaces/e1.31-dmx/>, <https://kno.wled.ge/interfaces/ddp/>, <https://kno.wled.ge/interfaces/udp-notifier/>.
- Linux config defaults should follow the XDG Base Directory Specification: `$XDG_CONFIG_HOME` when set, otherwise `$HOME/.config`. Source: <https://specifications.freedesktop.org/basedir-spec/latest/>.
- macOS support/config files belong under the user's Library/Application Support area rather than exposed user documents. Source: <https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/FileSystemProgrammingGuide/FileSystemOverview/FileSystemOverview.html>.
- Candidate host dependencies may include HTTP/WebSocket, Zeroconf, MQTT, and audio libraries, but each selection must be justified in this plan with license, packaging impact, and the exact boundary where it connects to original `wled00` code.

## Current Repo Analysis

- `AGENTS.md`, `.github/copilot-instructions.md`, and `.github/agent-build.instructions.md` now point contributors to the native host workflow and `Native-Port-Plan.md`; keep them aligned as commands and CI evolve.
- `readme.md` describes WLED as ESP32/ESP8266 firmware and lists ESP-specific features such as access point mode, OTA, and compatible hardware. Native user-visible behavior changes must be reflected there when implementation tasks make them real.
- The active PlatformIO config, helper scripts, board definitions, Python requirements, and firmware-centric GitHub workflows have been removed from the repository path. Remaining legacy mentions live mostly in historical notes and usermod-era documentation that will be reduced as those areas are ported or removed.
- `wled00/wled.h` is the highest-impact dependency hub. It directly includes Arduino, WiFi/ETH, LittleFS, ESPAsyncWebServer, DNSServer, WiFiUDP, AsyncMqttClient, OTA, IR, ESP-NOW, and generated web headers.
- `wled00/wled.cpp` contains the runtime lifecycle, filesystem mount, WiFi setup, UDP/WebSocket setup, OTA/IR setup, heap/PSRAM watchdog logic, and main loop.
- `wled00/bus_manager.*` and `wled00/bus_wrapper.h` contain the current LED output boundary. Modify these original files to remove ESP physical bus drivers and reduce the host path to the render-buffer/core-light-engine boundary; do not add null, virtual, preview, or GPIO-backed output buses.
- `wled00/file.cpp`, `cfg.cpp`, `presets.cpp`, `playlist.cpp`, `colors.cpp`, ledmap code, and palette code expect a filesystem facade named `WLED_FS` and JSON files such as `cfg.json`, `wsec.json`, `presets.json`, palette files, and ledmaps.
- `wled00/wled_server.cpp`, `json.cpp`, `ws.cpp`, `xml.cpp`, `set.cpp`, and `wled00/data/` define the web/API surface. Preserve these source files and change their platform dependencies instead of duplicating their behavior elsewhere.
- `wled00/udp.cpp`, `e131.cpp`, `mqtt.cpp`, `hue.cpp`, `alexa.cpp`, and `ntp.cpp` are mostly portable protocol logic mixed with Arduino networking types. Keep the protocol logic and replace transport/runtime dependencies in place.
- `wled00/wled.cpp` publishes `_wled._tcp` mDNS with a `mac` TXT property today, and `wled00/json.cpp` emits `info.mac`. Host WLED must preserve that externally visible identity contract even though the value is generated rather than read from WiFi hardware.
- `wled00/ws.cpp` and `wled00/data/index.js` already implement WebSocket state updates and live-view/peek behavior. Keep that compatibility path only as a view into the normal render buffer; do not add a new preview product surface.
- `wled00/um_manager.cpp` and `fcn_declare.h` preserve a usermod API based on `Usermod` and `REGISTER_USERMOD`. Keep this harness and the capability to register new host-compatible usermods, but remove bundled ESP/GPIO/peripheral usermods from the native product path.
- The previous top-level `native/` directory has been removed. Keep it removed unless a future task needs a non-product scratch/reference path; the final WLED application must compile original `wled00` sources, not a parallel native codebase.
- `usermods/audioreactive/` is the most valuable hardware-adjacent usermod to port because many built-in effects consume its data through `UsermodManager::getUMData(..., USERMOD_ID_AUDIOREACTIVE)`.
- A broad search found many files using Arduino or ESP-adjacent constructs such as `String`, `Print`, `File`, `IPAddress`, `Serial`, `millis()`, `delay()`, `PROGMEM`, `ESP32`, `ESP8266`, LittleFS, ESPAsyncWebServer, WiFiUDP, NeoPixelBus, GPIO, I2S, RMT, and pin APIs. The migration must be staged, but staged work still edits the original source files.

## Configuration Storage Recommendation

Use a host config root with the same WLED JSON file names inside it:

- Command-line override: `--config-dir <path>` has highest priority.
- Environment override: `WLED_CONFIG_DIR=<path>` is second priority.
- Linux default: `$XDG_CONFIG_HOME/wled` if set, otherwise `~/.config/wled`.
- macOS default: `~/Library/Application Support/WLED`.
- Development default: allow `--config-dir ./.wled-native` for local repeatable tests.

Keep `cfg.json`, `wsec.json`, `presets.json`, `palette*.json`, `ledmap*.json`, `remote.json`, uploaded assets, and user-created files under that root. Generate the same default configuration shape WLED uses on first run instead of auto-importing existing ESP exports. Add an explicit import command later only if needed; imports must be opt-in, non-destructive, and tested against original WLED config files.

## Feature Disposition Matrix

Keep this matrix in `Native-Port-Plan.md`; do not move it to a separate status file.

| Feature area | Target disposition | Notes |
|---|---|---|
| Browser UI in `wled00/data/` | Keep and update in place | Test changed pages through the normal web build and browser smoke checks. |
| JSON API and WebSocket API | Keep | Preserve Home Assistant-visible behavior and `/ws` state push semantics. |
| Effects, palettes, segments, transitions | Keep | Modify `wled00/FX*`, `colors.*`, and bus/render code directly as dependencies are removed. |
| Presets, playlists, ledmaps, palettes on disk | Keep | Store under the host config root with existing file names and JSON shapes. |
| Internal render buffer | Keep as core light-engine state, not as an output backend | The render buffer feeds effects, JSON/WebSocket/live-view compatibility, tests, and host state. Do not model it as a null, virtual, preview, or stub output. |
| Physical ESP LED drivers, GPIO, RMT, I2S, LEDC, HUB75, MY92xx, PWM buses | Remove | Remove from the host product path immediately after the runtime foundation. Do not replace them with temporary output shims. Host physical/network output backends are out of scope unless explicitly approved later. |
| Network realtime protocols: UDP sync, DDP, E1.31, Art-Net | Keep when host networking supports them as control/input protocols | Replace transport dependencies in original protocol files; do not keep ESP transport coupling and do not create output buses as substitutes. |
| WiFi AP/provisioning/scanning, ESP Ethernet boards | Remove or replace semantics | Host WLED uses OS networking; update UI/API fields to avoid misleading ESP controls. |
| Zeroconf/mDNS `_wled._tcp` | Keep | Stable generated MAC-compatible identity must match discovery TXT and `/json/info.mac`. |
| MQTT | Keep | Replace client dependency while preserving topics, payloads, publishes, callbacks, and reconnect behavior. |
| Hue and Alexa-compatible behavior | Keep where feasible | Validate multicast/SSDP/firewall behavior on macOS and Linux. |
| OTA firmware update and bootloader paths | Remove/replace with host package-update documentation | Endpoints/entities must not imply ESP firmware flashing exists. |
| Hardware IR receiver and hardware DMX UART | Remove | Network DMX/control protocols remain separate and should be preserved as host networking features. |
| Audioreactive usermod | Keep | Use OS default microphone first and preserve the usermod data contract; degrade gracefully without permission/device. |
| Usermod harness | Keep | Preserve `Usermod`, `UsermodManager`, `REGISTER_USERMOD`, hooks, config, JSON, MQTT, UDP, and overlay extension points. Keep only host-compatible bundled usermods listed below; remove the rest. |
| Tests | Keep and expand | Update original tests and add basic host tests to the normal repository test flow. |
| PlatformIO/ESP tooling | Remove from native product workflow | Do not keep firmware tooling as a fallback for native tasks. If a task removes it earlier than final CI/package cleanup, update docs, tests, and workflows in the same change. |


## Bundled Usermod Disposition For Native WLED

The usermod harness stays. The native product must still support new host-compatible usermods through the existing registration and hook model. Bundled usermods, however, must be kept only when they can run without ESP support, GPIO pins, board peripherals, ESP-only libraries, or physical LED/output drivers.

Classification basis: repository scan of `usermods/` source and manifests for GPIO/pin APIs, I2C/SPI/UART, ESP/Arduino-only headers, sensor/display libraries, output-driver libraries, filesystem use, and network use. `millis()`, `String`, JSON, WLED filesystem, WLED strip/segment APIs, and WLED network hooks are portability work when the usermod otherwise maps to host state, host networking, or the render buffer.

Keep and port these bundled usermods:

- Core harness/example/custom-effect support: `EXAMPLE` after making the example host-safe, `user_fx`.
- Audio/network/control integrations: `audioreactive` with OS microphone/audio input replacing ESP I2S/ADC, `Artemis_reciever`, `boblight`, `project_cars_shiftlight`, `smartnest`, `udp_name_sync`, `wizlights`, `usermod_v2_HttpPullLightControl`, `usermod_v2_klipper_percentage`.
- State, timer, clock, and render-buffer effects that do not require GPIO/peripherals after normal WLED portability work: `Analog_Clock`, `Cronixie`, `pov_display`, `PS_Comet`, `seven_segment_display`, `seven_segment_display_reloaded`, `stairway_wipe_basic`, `TetrisAI_v2`, `usermod_v2_animartrix`, `usermod_v2_auto_save`, `usermod_v2_brightness_follow_sun`, `usermod_v2_ping_pong_clock`, `usermod_v2_word_clock`, `word-clock-matrix`.

Remove these bundled usermods from the native product path because they depend on ESP-only board behavior, GPIO pins, ADC, I2C/SPI/UART peripherals, sensors, displays, relays, PWM/fans, hardware LED/output drivers, RF/IR devices, SD-card hardware, ESP power modes, ESP-specific network fixes, or ESP-specific VPN libraries:

- `ADS1115_v2`, `AHT10_v2`, `Animated_Staircase`, `Battery`, `battery_keypad_controller`, `BH1750_v2`, `BME280_v2`, `BME68X_v2`, `buzzer`, `deep_sleep`, `DHT`, `EleksTube_IPS`, `Enclosure_with_OLED_temp_ESP07`, `Fix_unreachable_netservices_v2`, `INA226_v2`, `Internal_Temperature_v2`, `JSON_IR_remote`, `LD2410_v2`, `LDR_Dusk_Dawn_v2`, `MAX17048_v2`, `mpu6050_imu`, `multi_relay`, `MY9291`, `photoresistor_sensor_mqtt_v1`, `PIR_sensor_switch`, `pixels_dice_tray`, `PWM_fan`, `pwm_outputs`, `quinled-an-penta`, `RelayBlinds`, `rgb-rotary-encoder`, `rotary_encoder_change_effect`, `RTC`, `sd_card`, `sensors_to_mqtt`, `sht`, `Si7021_MQTT_HA`, `SN_Photoresistor`, `ST7789_display`, `Temperature`, `TTGO-T-Display`, `usermod_rotary_brightness_color`, `usermod_v2_four_line_display_ALT`, `usermod_v2_RF433`, `usermod_v2_rotary_encoder_ui_ALT`, `VL53L0X_gestures`, `Wemos_D1_mini+Wemos32_mini_shield`, `wireguard`. If later implementation review finds any kept clock/matrix usermod still requires physical matrix hardware assumptions rather than only the WLED render buffer, remove that usermod in the same cleanup step instead of keeping an incompatible shell.

Removal rule: do not keep removed usermods as source examples, excluded build folders, disabled compatibility shells, or documentation promises in the native product. If a future contributor wants one back, it must be reintroduced as a host-compatible usermod with explicit host dependencies, tests, and documentation.

## Plan Maintenance Rules For Every Change

This plan is one unified migration checklist. The sections below are accomplishment areas, not sequential tasks. Work can move across multiple areas in the same change as dependencies require, but every change should leave this file more accurate than before.

Before finishing any native-port change:

1. Update the relevant area log in this file with actions taken, deviations, key decisions, verification performed, and newly discovered tasks or risks.
2. If execution reveals a missing dependency that blocks a host-feasible feature, expand the relevant area to modify additional original WLED files, shared utilities, tests, or build rules. If the dependency is ESP/GPIO/peripheral-only, remove that source path instead of preserving it as a blocker, placeholder, or compatibility shell.
3. Update `readme.md`, `AGENTS.md`, `.github/` instructions, or other existing contributor docs only when commands, supported platforms, configuration paths, build systems, or user-visible behavior actually change. Do not create separate native-port status documents.
4. Update original tests as needed and add basic new tests in the same normal test flow. If a new test runner is introduced, wire it into the repository scripts/CI so it does not become ignored side coverage.
5. Run the meaningful validation loop available for the changed area and record failures that are environmental, incomplete by design, or intentionally deferred.

## Unified Migration Checklist

Current implementation state:
- The host build currently compiles `wled00/wled_main.cpp` plus host-side runtime/storage/server helpers under `wled00/`, starts a native HTTP/WebSocket process by default, and still exposes bootstrap/status reporting plus file/config helper subcommands through the same entry point.
- The native CLI now covers config-root resolution, identity persistence, logical-path validation, file read/copy/rename/delete, file compare, JSON validation, backup/restore, backup existence checks, and JSON-file listing inside the config root.
- Original runtime files such as `file.cpp`, `cfg.cpp`, `presets.cpp`, and `playlist.cpp` are already part of the host binary, but the real `wled.cpp` runtime loop, original HTTP/WebSocket handlers, Zeroconf, and network protocol stack are still not part of the native runtime path.

- [x] Establish the in-place migration baseline, inventory tests/build outputs, and redirect contributor guidance back to this plan.
- [x] Add a host build/run entry point that compiles original `wled00/` sources and exposes a stable CLI contract.
- [ ] Make original WLED startup, timing, filesystem, configuration, identity, logging, and shutdown work on macOS/Linux.
- [ ] Remove ESP-only hardware semantics, unsupported bundled usermods, misleading UI/API fields, and physical output drivers from the host product path.
- [ ] Bring up the core light engine around the internal render buffer while preserving effects, presets, playlists, timers, and live-view compatibility.
- [ ] Serve the existing browser UI, JSON API, and WebSocket API from the host process using original WLED handlers.
- [ ] Replace WiFi/ETH assumptions with host networking, Zeroconf, realtime UDP protocols, and time services.
- [ ] Preserve MQTT, Hue/Alexa-compatible behavior, the audioreactive usermod, the usermod harness, and only the kept bundled usermods.
- [ ] Complete the repository transition to native CI, packaging, service helpers, install docs, and host-first contributor workflows.

## Already Accomplished

### Baseline, scope, test inventory, and contributor guidance

Status: complete for this phase.

Completed work:
- Confirmed the branch target remains in-place migration of `wled00/` and that `wled00-backup/` is present only as an ignored reference snapshot.
- Inventoried the current automated test surface: `npm test` runs the Node.js test runner, `tools/cdata-test.js` validates web asset generation, `test/` only contained the PlatformIO README baseline before native task work, and generated web headers remain ignored artifacts under `wled00/`.
- Added a native CLI smoke test entry under `test/native-cli.test.js` so the normal `npm test` flow now covers the host bootstrap path.
- Updated contributor-facing docs (`readme.md`, `AGENTS.md`, `.github/copilot-instructions.md`, `.github/agent-build.instructions.md`) to point native-port work back to this plan and the new `scripts/native-*.sh` wrappers instead of any separate native status document.

Discrepancies or deviations:
- The repository still contains PlatformIO firmware tooling and workflows because the baseline phase only established the inventory and native workflow entry points; removal remains part of the broader repository transition.

Key decisions and reasoning:
- Kept this phase focused on inventory and documentation because the repo already did not reference `docs/native-porting.md` or `docs/native-running.md`; the missing baseline gap was explicit native workflow/test inventory rather than stale links.
- Reused the existing Node.js test runner for the new native smoke test so native validation enters the normal repository test flow immediately instead of creating an isolated side harness.

Verification performed:
- Ran `node --test test/native-cli.test.js` before implementation and confirmed failure because `scripts/native-run.sh` did not yet exist.
- Ran `npm ci`, `npm test`, and `npm run build` after the baseline/bootstrap changes landed.
- Attempted `pio run -e esp32dev`, but `pio` is not installed in this environment (`command not found`), so firmware-baseline verification remains pending on a PlatformIO-capable machine.

Open risks / follow-up:
- Host runtime work needs a reusable way to expose package version/build metadata to more of `wled00/` than the standalone bootstrap CLI currently touches.

### Host build/run entry point for the original `wled00` tree

Status: complete for this phase.

Completed work:
- Added a root `CMakeLists.txt` that extracts the version from `package.json` and builds a `wled-native` executable from source files living under `wled00/`.
- Added `wled00/wled_host_cli.h`, `wled00/wled_host_cli.cpp`, `wled00/wled_host_runtime.h`, and `wled00/wled_host_runtime.cpp` as the first host-only bootstrap layer for the original source tree, with `wled00/wled_main.cpp` now serving as the native entry point.
- Added `wled00/wled_host_storage.h` and `wled00/wled_host_storage.cpp` as the host-side storage/bootstrap layer used by the current native CLI.
- Added `scripts/native-build.sh`, `scripts/native-run.sh`, and `scripts/native-test.sh` as thin wrappers around the in-place host build and CLI smoke path.
- Added `native:build`, `native:run`, and `native:test` npm scripts so the host workflow is discoverable through the existing developer tooling.

Discrepancies or deviations:
- The bootstrap executable currently parses and reports CLI options and exercises the host storage/bootstrap layer, but it still does not call `WLED::instance().setup()` / `loop()`; that work belongs to the runtime foundation area because the current `wled00` runtime still hard-depends on Arduino/ESP headers and lifecycle assumptions.

Key decisions and reasoning:
- Used CMake because it is already available on macOS/Linux developer machines and can target original `wled00/` sources without introducing a second application tree.
- Kept the first native compile target intentionally small so the host path now has a stable command-line contract (`--help`, `--version`, `--config-dir`, `--host`, `--port`, `--log-level`) before broader de-ESP runtime work begins.
- Expanded that stable contract to include config-root file-management commands (`--resolve-path`, `--read-file`, `--copy-file`, `--rename-file`, `--delete-file`, `--compare-files`, `--validate-json`, `--backup-file`, `--restore-file`, `--has-backup`, `--list-files`) so the storage/bootstrap work is validated through the same entry point.
- Parsed the package version directly from `package.json` during CMake configure so the host CLI reports the same version string as the existing web build tooling without a new duplicate version file.
- Chose CMake as a build-time-only dependency boundary for the host path; it adds no runtime dependency to the produced binary, and current packaging impact is limited to requiring CMake plus a C++17 compiler on developer/build machines.

Verification performed:
- Added `test/native-cli.test.js` first, then confirmed the red failure before writing the host scaffold.
- Ran `node --test test/native-cli.test.js`, `npm test`, `npm run build`, `scripts/native-build.sh`, `scripts/native-run.sh --help`, and `scripts/native-test.sh` successfully after implementation.
- Re-ran `scripts/native-build.sh`, `node --test test/native-cli.test.js`, `scripts/native-test.sh`, `npm run build`, and `npm test` after extending the bootstrap CLI/storage commands and moving the native entry point into `wled00/wled_main.cpp`.

Open risks / follow-up:
- Future native binaries, tests, and runtime code should share one host-facing configuration/bootstrap layer so CLI parsing or config-root resolution logic does not get duplicated.

## Migration Accomplishment Areas

### Host runtime foundation, filesystem, configuration, identity, and process lifecycle

Objective: make original WLED startup, timing, filesystem, configuration, identity, logging, and shutdown work on macOS/Linux.

Things to accomplish:
- Modify `wled00/wled.h`, `wled00/wled.cpp`, `wled00/wled_main.cpp`, `file.cpp`, `cfg.cpp`, `presets.cpp`, and related files so host builds use direct host implementations instead of ESP/Arduino facilities.
- Replace ESP-only lifecycle assumptions with a host process lifecycle that still preserves WLED setup/loop ordering where behavior depends on it.
- Provide host implementations for timing, sleeping, restart request semantics, logging, filesystem access, and memory diagnostics through small shared utilities only where repeated source edits would be worse.
- Implement host config root resolution and keep WLED's existing JSON file names and default shapes.
- Generate and persist a stable MAC-compatible instance ID in the config root for `/json/info`, mDNS TXT, logs, and Home Assistant identity.
- Replace automatic ESP config import assumptions with explicit future import only.

Key files:
- `wled00/wled.h`
- `wled00/wled.cpp`
- `wled00/wled_main.cpp`
- `wled00/file.cpp`
- `wled00/cfg.cpp`
- `wled00/presets.cpp`
- `wled00/playlist.cpp`
- `wled00/json.cpp`
- Host utility files under `wled00/` if needed
- `tools/cdata-test.js`
- `test/`

Dependency-expansion rule:
- If startup/config work requires additional WLED globals, JSON helpers, or filesystem callers to change, expand this area to modify those original files. Do not skip configuration, presets, or identity because a helper is missing.

Implementation notes:
- Reject path traversal and absolute-path escapes when serving or writing files through WLED APIs.
- Replace ESP watchdog/heap-panic behavior with host diagnostics and clean exit/restart request semantics.
- Avoid busy loops that burn CPU on host systems.
- Keep secrets such as `wsec.json` inside the config root, not generated build directories.
- Preserve the original startup dependency order from `wled.cpp`: filesystem/config recovery, config deserialize, strip/bus finalization, usermod setup, route registration, then interface/service bring-up. Native startup can replace the WiFi-specific trigger points, but it cannot safely start discovery, WebSocket/JSON state serving, or LED output before config and bus state exist.

Tests and verification:
- Update existing tests for any changed web asset/config assumptions.
- Add basic tests for config root selection, default file creation, read/write/delete/list/copy behavior, path traversal rejection, persisted identity stability, and graceful shutdown/restart code.
- Run host CLI/runtime tests through the repository test scripts, plus `npm test` and `npm run build` when relevant.

Area log:
- Actions taken:
- Re-read the startup flow in `wled00-backup/` and the active `wled00/` tree to map the original boot ordering before continuing native server work.
- Added a native default server mode to the host runtime so `scripts/native-run.sh` now stays up by default, while `--exit-after-bootstrap` preserves the non-serving diagnostic path for smoke tests and scripted storage/config operations.
- Added a host HTTP/WebSocket server scaffold under `wled00/` that serves `/`, `/json`, `/json/info`, `/json/state`, `/json/si`, `/json/effects`, `/json/palettes`, `/json/fxdata`, `/json/cfg`, `/json/nodes`, `/json/pins`, `/version`, and `/ws` from the current native runtime state.
- Added a host runtime bootstrap layer under `wled00/` that resolves the config root using `--config-dir`, `WLED_CONFIG_DIR`, macOS Application Support, and Linux XDG defaults.
- Switched the native executable entry point from the standalone `wled00/wled_host_main.cpp` shim to the original `wled00/wled_main.cpp`, with the host path delegating to a new in-tree runtime bootstrap.
- Added persisted host identity bootstrap via a config-root `instance-id` file containing a locally administered MAC-compatible identifier for future `/json/info.mac` and Zeroconf work.
- Added host bootstrap creation of the canonical WLED config/preset filenames (`cfg.json`, `wsec.json`, `presets.json`, `tmp.json`) so future runtime work can move persistence behind native-host storage without relying on ESP filesystem setup.
- Added host logical-path resolution with rejection of path traversal and config-root escapes, establishing the secure path boundary required before porting original filesystem APIs.
- Added host read/copy primitives for logical config-root files, giving the next `file.cpp` portability pass secure building blocks for bootstrap-safe file operations.
- Added host rename/delete primitives for logical config-root files so the host storage contract now covers the next `file.cpp` restore/backup cleanup operations without inventing a separate filesystem shim.
- Added host compare, JSON validation, backup, restore, backup-existence, and root-listing primitives so the native storage layer now covers most of the original `file.cpp` helper surface before wiring that code into host builds.
- Expanded native smoke coverage so the Node test flow and `scripts/native-test.sh` verify config-root resolution and identity persistence in addition to `--help` and `--version`.
- Added host wrappers for original `cfg.cpp` backup, restore, reset, validation, and secrets-validation helpers so the native CLI now exercises more of the original config lifecycle surface through in-place source files instead of bootstrap-only storage helpers.
- Moved host color-helper ownership from the duplicated `wled_host_core.cpp` into the original `wled00/colors.cpp` / `wled00/colors.h` files, keeping only host random-wheel helpers in `wled_host_core.cpp`.
- Added a native CLI `--add-color` command backed by `color_add()` and extended the smoke tests so the host path now validates blend, add, and fade behavior through the original `colors.cpp` source file.
- Restored the missing `wled00/data` source tree from the `wled00-backup/data` recovery snapshot so the normal web build and `cdata`-based validation flow work again in the original product tree.
- Hardened `tools/cdata-test.js` so it restores `wled00/data`, `tools/cdata.js`, and `package.json` reliably after interrupted runs and validates rebuild behavior using direct file mtime comparisons instead of brittle wall-clock thresholds.
- Discrepancies or deviations:
- This phase still does not generate the full runtime-populated `cfg.json`/`wsec.json` schema; it only seeds valid bootstrap JSON files because the original config/schema serialization code is still coupled to Arduino/AsyncWebServer types and needs a larger in-place runtime extraction.
- The restored `wled00/data` tree currently comes from the frozen `wled00-backup` snapshot, so later UI work should treat it as recovered source and continue editing only `wled00/data`, not the backup tree.
- The current host server is an in-tree scaffold, not the original `wled_server.cpp` / `ws.cpp` / `json.cpp` path yet. It preserves the basic runtime contract and test surface, but it does not count as completion of the original handler port.
- Key decisions and reasoning:
- Treat the backup snapshot as confirmation of the intended startup contract rather than as a second implementation: both trees show that WLED startup is not “start HTTP first”, it is “load config, build strip/bus state, then expose services”.
- Kept the host runtime entry point unified instead of creating a separate server binary, so bootstrap diagnostics, CLI helpers, and the default server path all share the same config-root resolution and identity bootstrap.
- Allowed `--port 0` in the host CLI so tests can allocate ephemeral local ports safely without hard-coding a listening port in parallel test runs.
- Chose a simple `instance-id` text file in the config root for the first persisted identity step so the host bootstrap can provide stable Home Assistant-compatible identity without prematurely inventing a partial `cfg.json` schema.
- Seeded the real WLED filenames with minimal valid JSON instead of inventing a broad host `FS`/`File` shim, which keeps the next filesystem/config extraction focused on replacing direct `WLED_FS` usage in original source files.
- Moved the native entry point into `wled00/wled_main.cpp` so the host build advances through original source files instead of growing a separate top-level runtime executable path.
- Kept the new JSON validation local to the host bootstrap/storage layer rather than pulling ArduinoJson into the bootstrap binary, because the immediate goal is structural parity for `file.cpp` helpers while the original config/preset code is still not part of the host compile.
- Extended the host CLI rather than creating a test-only helper so the same config-root file operations remain available for future bootstrap debugging while the original storage code is still being extracted.
- Pulled host color logic into the original `colors.cpp` file with a focused non-Arduino branch instead of growing the duplicate host color module further, because the plan requires using original `wled00/` sources wherever feasible and color math is already a stable, well-bounded portability target.
- Restored `wled00/data` into the main tree instead of teaching the build/test workflow to read from `wled00-backup`, because the migration target remains the original `wled00/` product tree and the backup snapshot is only acceptable as recovery input.
- Reworked the `cdata` test assertions around actual output mtimes and explicit “already built” output so the web-build regression suite measures the real rebuild contract instead of depending on sub-second timing assumptions that break on slower machines.
- Verification performed:
- Updated the native CLI tests to assert config-root selection and repeated identity stability for the same config directory.
- Updated the native CLI tests to assert bootstrap creation of `cfg.json`, `wsec.json`, `presets.json`, and `tmp.json`.
- Updated the native CLI tests and wrapper smoke script to verify logical-path resolution and traversal rejection inside the config root.
- Updated the native CLI tests and wrapper smoke script to verify secure logical-file reads and copies inside the config root.
- Updated the native CLI tests and wrapper smoke script to verify secure logical-file renames and deletions inside the config root.
- Added native CLI tests and wrapper smoke coverage for file comparison, JSON validation, backup creation, backup restoration, backup existence checks, and root JSON-file listing that hides secrets files.
- Added native CLI tests and wrapper smoke coverage for the original `cfg.cpp` helper path plus `color_add()` routed through `wled00/colors.cpp`.
- Added native CLI tests for default server startup, JSON route responses, HTTP state updates, and WebSocket state propagation, then ran them successfully outside the sandbox because the sandbox blocks local listening sockets.
- Restored `wled00/data`, then ran `node --test tools/cdata-test.js`, `npm run build`, `npm test`, and `scripts/native-test.sh` successfully from the recovered baseline.
- PlatformIO firmware validation was not rerun in this environment because the current native task only touched host-only files and the repo still lacks a verified local `pio` toolchain here.
- Newly discovered tasks or risks:
- `initServer()` is only route registration; the actual runtime bring-up is deferred until `handleConnection()` reaches `initInterfaces()`. A native port that starts serving before replacing that later stage will be observably incomplete even if an HTTP listener exists.
- The current host bootstrap still lacks the original `beginStrip()` / `strip.finalizeInit()` path, so it cannot honestly claim “WLED started” for users who expect configured buses, presets, playlists, and service-loop-driven effects to be live at process start.
- The host server scaffold currently serves a minimal runtime state that is not backed by the original render loop, segment engine, or network integrations, so browser/API compatibility will drift until the real `wled.cpp` lifecycle is ported.
  - A shared host-facing configuration/bootstrap layer is needed so runtime code, tests, and future binaries do not duplicate CLI parsing or config-root resolution logic.
  - Package version/build metadata likely needs a reusable interface inside `wled00/`, not only the standalone bootstrap CLI.
  - The next runtime pass needs to replace Arduino-bound config serialization so host bootstrap can create real WLED config files instead of only the identity seed.
  - The restored `wled00/data` content should eventually be compared against the active trunk source if newer UI changes are expected elsewhere, because the recovery snapshot may lag future upstream edits even though it unblocks the local build/test flow.
  - `file.cpp` still needs to consume the new host storage primitives directly from original source builds; the helper surface now exists, but `cfg.cpp`, `presets.cpp`, and the rest of the original runtime are still not compiled into the host binary.

### ESP-only hardware removal, unsupported bundled usermods, UI/API cleanup, and physical output removal

Objective: remove host-impossible ESP hardware paths so later native work cannot depend on ESP fallbacks.

Things to accomplish:
- Remove ESP-only features from original code: WiFi AP/provisioning, ESP-NOW, OTA firmware/bootloader update, brownout/watchdog, flash/PSRAM reporting, GPIO pin validation, pin manager behavior that only protects ESP GPIOs, RMT/I2S/LEDC, HUB75, MY92xx, PWM/fan output paths, IR receiver GPIO, hardware DMX UART, ESP Ethernet boards, PlatformIO board settings, and ESP-specific reset/update logic.
- Update settings pages and JSON/API info fields to remove ESP pin, board, flash, PSRAM, WiFi provisioning, AP, OTA, and physical-output controls. Keep only stable documented host values or compatibility fields with explicit host semantics.
- Remove physical ESP LED output controls and bus/output configuration from host settings. Do not replace them with null, virtual, stub, preview, or browser-only outputs.
- Preserve network DMX/control protocols such as E1.31/Art-Net/DDP as host networking/control features, not as substitute output buses.
- Remove hardware-only bundled usermods listed in the disposition section from native build/docs/product source paths while keeping `Usermod`, `UsermodManager`, registration, config, JSON, MQTT, UDP, and overlay hooks available for kept and new host-compatible usermods.
- Remove dead code rather than leaving silent preprocessor rubble once reference searches and tests prove it is unused.

Key files:
- `wled00/wled.h`
- `wled00/wled.cpp`
- `wled00/pin_manager.*`
- `wled00/ota_update.*`
- `wled00/ir.cpp`
- `wled00/dmx_input.*`
- `wled00/dmx_output.cpp`
- `wled00/wled_ethernet.h`
- `wled00/bus_manager.*`
- `wled00/bus_wrapper.h`
- `wled00/data/settings_*.htm`
- `wled00/data/index.js`
- `wled00/um_manager.cpp`
- `wled00/fcn_declare.h`
- `usermods/`
- `usermods/readme.md`
- `wled00/const.h`
- `readme.md`
- `AGENTS.md`

Dependency-expansion rule:
- If removing ESP semantics uncovers integration-visible fields or behaviors, either document stable host semantics and test them or remove the field with a documented breaking change. Do not keep ESP-only fields as inert compatibility shells.

Implementation notes:
- Highlight all user-visible breaking changes in this plan, `readme.md`, and release notes when they exist.
- Home Assistant restart/update entities should map to documented host restart/package-update behavior rather than ESP reboot/OTA firmware flashing; if host package-update behavior is not implemented yet, remove or clearly report unsupported update behavior instead of exposing ESP OTA semantics.
- Keep fields only when they have stable, documented host values or compatibility reason.

Tests and verification:
- Update web and JSON tests for removed/replaced fields.
- Browser settings pages must contain no misleading GPIO/ESP board/output controls.
- Tests prove removed endpoints return documented host behavior.
- Native build must no longer depend on ESP Arduino headers, PlatformIO libraries, NeoPixelBus/RMT/I2S/LEDC/HUB75/MY92xx/PWM output libraries, or removed usermod dependencies for host runtime.
- Run reference searches for ESP-only APIs and document intentional leftovers.

Area log:
- Actions taken:
  - Plan update: moved ESP hardware cleanup forward in the migration so it follows the host runtime foundation rather than remaining late cleanup.
  - Plan update: added unsupported bundled usermod removal to this area so hardware-only usermods cannot remain as native build/documentation baggage.
- Discrepancies or deviations:
  - Earlier plan versions deferred cleanup until host equivalents were functional; this now conflicts with the native-only direction and has been replaced with early removal of ESP/GPIO/peripheral paths.
- Key decisions and reasoning:
  - Remove physical output drivers, GPIO semantics, ESP board fields, and unsupported usermods before broader rendering/network/integration work so later changes cannot accidentally preserve ESP-only abstractions.
- Verification performed:
  - Reviewed plan ordering and searched for old late-cleanup and stub/null-output wording.
- Newly discovered tasks or risks:

### Render buffer, core light engine, effects, presets, playlists, and timers

Objective: bring up WLED's core light engine in the original source tree with the render buffer as application state, not as a physical/null/virtual output path.

Things to accomplish:
- Modify `wled00/bus_manager.*`, `bus_wrapper.h`, `FX*.cpp`, `FX*.h`, `colors.*`, `palettes.cpp`, `led.cpp`, `playlist.cpp`, `presets.cpp`, `json.cpp`, and related files so host systems render into WLED's internal frame/state buffer without ESP physical output buses.
- Preserve segment rendering, color order, RGBW/CCT metadata where meaningful, transitions, presets, playlists, nightlight, timers, palettes, 1D effects, and 2D effects that can run without unavailable hardware. Keep `BusManager` only if it is slimmed to render-buffer/core-light-engine coordination; remove ESP bus subclasses and output-driver semantics.
- Remove NeoPixelBus/RMT/I2S/LEDC/HUB75/MY92xx/PWM physical output dependencies from the host build. Do not add `BusNull`, virtual outputs, stub outputs, preview outputs, GPIO-backed outputs, or other interim output backends.
- Keep the normal render buffer as the source for existing live-view/peek behavior; do not add a separate browser preview feature.
- Keep network realtime protocols out of this area except where core state hooks are needed; the host networking/control protocol work must not reintroduce output buses.

Key files:
- `wled00/bus_manager.cpp`
- `wled00/bus_manager.h`
- `wled00/bus_wrapper.h`
- `wled00/FX.cpp`
- `wled00/FX_fcn.cpp`
- `wled00/FX_2Dfcn.cpp`
- `wled00/FX.h`
- `wled00/colors.cpp`
- `wled00/colors.h`
- `wled00/palettes.cpp`
- `wled00/led.cpp`
- `wled00/presets.cpp`
- `wled00/playlist.cpp`
- `wled00/json.cpp`
- `wled00/data/index.js` only if live-view semantics require UI adjustments

Dependency-expansion rule:
- If effect compilation reveals missing timers, random sources, palette storage, JSON state helpers, or render dependencies, expand this area to modify those original WLED files. Do not replace full host-feasible effects with permanent placeholders; remove only effects or code paths that are intrinsically tied to removed hardware.

Implementation notes:
- Treat compile failures in hot-path code as portability work, not reasons to drop effects.
- Replace ESP-specific performance attributes with empty or compiler-appropriate host attributes where needed.
- Preserve deterministic behavior for tests by adding injectable clock/random hooks only where needed and keeping production behavior WLED-compatible.
- Do not plan future output backends in this area; this migration is focused on removing ESP-only outputs and preserving the host render state.

Tests and verification:
- Update original tests affected by effect/palette/config serialization changes.
- Add basic host tests for color conversion, configured render length, color order, RGBW/CCT metadata, selected effects rendering non-empty frames into the render buffer, preset save/apply, playlist advancement, nightlight fade, and timer behavior.
- Test that `/ws` live-view frames, when retained, read from the normal render buffer.
- Run host build/tests plus `npm test`; run browser checks once the server/API area is available.

Area log:
- Actions taken:
  - Plan update: removed memory/null/no-op output backend language and reframed the render buffer as core WLED application state.
  - Host-enabled the original `wled00/prng.h` logic and lifted the 32-bit color helper algorithms from `wled00/colors.cpp` into narrow host support code so the native binary now runs real WLED core utilities beyond config/bootstrap.
  - Added `wled00/wled_host_core.h` and `wled00/wled_host_core.cpp` as narrow host support for random-number plumbing plus the currently-needed color utility functions.
  - Extended the existing `wled-native` CLI with color blend, color fade, and PRNG-sequence commands so the host binary now executes WLED core algorithms instead of only config/bootstrap helpers.
  - Host-enabled the original `wled00/playlist.cpp` state machine by adding a narrow host playlist boundary for time advancement, transition tracking, and preset-apply callbacks, then wiring a `--playlist-run` CLI path that loads playlist JSON from the config root and advances the original playlist logic against host ticks.
  - Host-enabled the original `wled00/presets.cpp` file for the preset-file bootstrap/name/delete slice by adding a narrow host preset header, wiring the active config-root layout into original preset callers, and compiling `presets.cpp` into the native binary instead of keeping preset-file behavior in generic storage helpers only.
  - Host-enabled the original `wled00/file.cpp` helper surface for the current host build by adding a host branch that resolves WLED logical paths against the active config root and provides original object-file read/write, copy, backup, restore, backup-existence, JSON validation, and dump helpers on macOS/Linux.
  - Switched the host `presets.cpp` branch to call the new original `file.cpp` host helpers for preset object reads/writes, so preset bootstrap/name/delete no longer maintain a duplicate file-format implementation.
  - Host-enabled the original `wled00/cfg.cpp` wrapper surface for config-file control by compiling a host branch that now routes `cfg.json` backup, restore, verify, backup-existence, reset, and `wsec.json` validation through the original config entry points.
  - Extended the native CLI with `--backup-config`, `--restore-config`, `--verify-config`, `--reset-config`, `--has-config-backup`, and `--verify-secrets` so the host process now exercises original `cfg.cpp` wrappers through the same runtime entry point.
  - Extended the native CLI with `--init-presets`, `--preset-name`, and `--delete-preset` so the host process now exercises original preset-file naming/bootstrap/delete behavior through the same entry point used for other host runtime checks.
  - Added native smoke coverage for the new core CLI path in `test/native-cli.test.js` and `scripts/native-test.sh` rather than creating a native-only side harness.
- Discrepancies or deviations:
  - Earlier plan versions allowed a null output backend as a first milestone; this was removed because it could become an interim output architecture rather than native cleanup.
  - The first host-core slice stopped at color and PRNG support; the next passes expanded into `playlist.cpp` and only then into the file-oriented parts of `presets.cpp`, instead of forcing the full preset apply/save/state-deserialization pipeline into the host build prematurely.
  - FastLED-derived palette/Hue helpers were not pulled into the host binary for this slice because the current CLI/runtime checks do not need them yet; keeping them out avoids a premature `pgmspace`/palette dependency before the actual render path is ported.
  - The current `presets.cpp` host branch intentionally stops at bootstrap/name/delete operations; `savePreset()`, `handlePresets()`, and preset application still depend on broader state/deserialization/runtime plumbing that should be ported together rather than stubbed piecemeal.
  - The current host `file.cpp` branch uses whole-document JSON rewrite semantics instead of the original in-place whitespace-preserving mutation strategy, because host correctness and integration with the existing config root matter more right now than preserving the ESP flash-optimization path.
  - The current `cfg.cpp` host branch intentionally stops at file-control wrappers; full config serialization/deserialization still depends on a much larger set of runtime globals, network settings, bus config, and usermod integration that should be ported as one runtime/config slice.
- Key decisions and reasoning:
  - Keep effects, presets, playlists, timers, segments, and live-view compatibility by rendering into the normal buffer, while deleting ESP bus/output subclasses instead of replacing them with virtual outputs.
  - Started the render/core migration with `colors.cpp` and `prng.h` because they are original WLED logic that can be ported in place with limited host support, which removes real Arduino coupling now without inventing a broad emulation surface.
  - Kept the host slice restricted to color blend/fade plus PRNG because those utilities are self-contained and useful immediately, while palette/Hue/FastLED-dependent functions belong to the later render-core bring-up once the surrounding WLED state is available.
  - Ported `playlist.cpp` by giving it a narrow host boundary for clock advancement, transition storage, and preset application callbacks, which keeps the original playlist state machine intact while removing its transitive `wled.h` dependency from the host build.
  - Ported the file-oriented `presets.cpp` operations next because they are a natural follow-on to playlist support and let the host binary start using original preset-file code before the full preset-application path is ready.
  - Ported `file.cpp` next because it is the shared storage dependency behind presets and future config/runtime work; moving preset-file operations onto original file helpers reduces duplicated host logic and prepares later `cfg.cpp` / `json.cpp` slices.
  - Ported the file-control portion of `cfg.cpp` next because it converts another public WLED config surface to original source ownership without forcing the full configuration schema/runtime graph into the host build prematurely.
- Verification performed:
  - Checked cross-area references so HTTP/browser validation remains tied to the web/API area and networking/control protocols remain tied to the host networking area.
  - Deferred verification until the full edited slice was complete, per user direction; fresh build/test results are recorded after this patch set lands.
  - Ran `scripts/native-build.sh`, `node --test test/native-cli.test.js`, and `scripts/native-test.sh` successfully after removing the unnecessary FastLED host dependency from this slice.
  - Extended the same native verification loop to cover `--playlist-run`; the rebuilt host binary now loads a config-root playlist JSON, advances the original `wled00/playlist.cpp` state machine for three 150 ms ticks, and reports the expected preset sequence `11 22 33` with a final transition of `700` ms.
  - Extended the same native verification loop to cover `--init-presets`, `--preset-name`, and `--delete-preset`; the rebuilt host binary now recreates `presets.json`, reads preset name `Evening` from preset `2`, and rewrites preset `2` as an empty object using original `wled00/presets.cpp` host logic.
  - Re-ran `scripts/native-build.sh`, `node --test test/native-cli.test.js`, and `scripts/native-test.sh` after compiling `wled00/file.cpp` into the host target and moving the preset host branch onto those original file helpers; the full native CLI loop still passes.
  - Extended the same native verification loop to cover `--backup-config`, `--restore-config`, `--verify-config`, `--reset-config`, `--has-config-backup`, and `--verify-secrets`; the rebuilt host binary now routes those operations through original `wled00/cfg.cpp` wrappers and passes both the Node CLI tests and the shell smoke script.
  - `npm run build` and therefore full `npm test` are currently blocked because `wled00/data/` is deleted in the working tree, so the web asset build cannot scan `wled00/data`; this is an existing workspace state issue outside the narrow host-core slice.
- Newly discovered tasks or risks:
  - `colors.h` and adjacent core headers still inherit many implicit transitive assumptions from `wled.h`; additional host-safe header cleanup is still needed before pulling in the full preset-apply path, `FX*.cpp`, or `json.cpp`.
  - The preset slice confirms the next render/core passes will need tightly scoped host-facing state headers for exact original globals/constants, but those boundaries should stay file-specific rather than growing into an Arduino-compatibility layer.
  - The next preset/runtime pass must replace the current placeholder preset-apply callback path with real `applyPreset()` / `handlePresets()` behavior, which likely pulls in parts of `deserializeState()`, notification/update hooks, and more shared strip/runtime globals.
  - The new host `file.cpp` branch still bypasses web-serving helpers such as `handleFileRead()` and the ESP file-cache path; those should be revisited when the HTTP server area is ported so browser/file routes use original code instead of bootstrap utilities.
  - The next config/runtime pass must decide how much of `deserializeConfig*()` and `serializeConfig*()` can be pulled in together; splitting them too finely risks duplicating schema/state logic that should stay owned by original `cfg.cpp`.
  - Reintroducing FastLED-derived palette/Hue helpers should wait for a slice that genuinely needs them; otherwise the host build accumulates avoidable compatibility baggage ahead of the render port.

### HTTP server, WebSocket server, web UI, and JSON API

Objective: serve the existing WLED browser UI and API from the host process using original WLED handlers as much as possible.

Things to accomplish:
- Modify `wled00/wled_server.cpp`, `ws.cpp`, `json.cpp`, `xml.cpp`, `set.cpp`, and related request/response code to use host HTTP/WebSocket primitives.
- Replace `ESPAsyncWebServer`, `AsyncWebSocket`, `AsyncCallbackJsonWebHandler`, and response/request classes by editing original server code and introducing narrow host request/response utilities only where shared behavior is needed.
- Serve existing `wled00/data/` assets or generated assets according to the selected build mode.
- Preserve `/`, `/settings`, `/json`, `/json/state`, `/json/info`, `/json/si`, `/ws`, `/edit`, `/upload`, `/version`, `/uptime`, palette routes, live view routes, and static JS/CSS routes where applicable. Keep `/freeheap` only if the hardware-cleanup area gives it documented host memory semantics; otherwise remove or replace it with a documented host diagnostics route.
- Ensure OTA/bootloader update endpoints removed by the hardware-cleanup area stay removed or report documented host package-update behavior; they must not imply ESP firmware flashing exists.
- Bind to localhost by default and make LAN exposure explicit through `--host 0.0.0.0` or config.

Key files:
- `wled00/wled_server.cpp`
- `wled00/ws.cpp`
- `wled00/json.cpp`
- `wled00/xml.cpp`
- `wled00/set.cpp`
- `wled00/data/`
- `tools/cdata.js`
- `tools/cdata-test.js`
- `package.json`
- Host HTTP/WebSocket utility files under `wled00/` if needed

Dependency-expansion rule:
- If preserving a host-feasible route requires more state, filesystem, segment, preset, playlist, or render support, expand this area to wire those dependencies into the original handlers. Do not duplicate a simplified API elsewhere; remove routes only when the hardware-cleanup area classifies their behavior as ESP-only and document the breaking change.

Implementation notes:
- Preserve Home Assistant's WebSocket expectations: connect to `/ws`, receive state/info on connect and state changes, accept JSON state updates, and support verbose replies.
- Browser testing is mandatory for changed pages.
- Existing live-view/peek compatibility remains allowed only as a view into the normal render buffer.
- The original firmware does not call `server.begin()` inside `initServer()`. Native HTTP/WebSocket startup should follow the same split of responsibilities: route registration first, bind/listen only after config and render state are initialized, then keep route behavior owned by original handlers.

Tests and verification:
- Update `tools/cdata-test.js` and existing web build tests when asset packaging changes.
- Add HTTP/API tests that start host WLED on a random local port, query JSON routes, POST state changes, observe WebSocket output, and verify render-buffer changes.
- Browser smoke tests must cover page load, console errors, navigation, color controls, effect/palette changes, settings pages, upload/list editor paths, WebSocket updates, live-view if retained, and mobile-sized viewport smoke checks.
- Run `npm run build`, `npm test`, host tests, and browser tests.

Area log:
- Actions taken:
- Startup analysis: traced `wled00-backup/wled.cpp` / `wled00/wled.cpp` and confirmed that `initServer()` only registers routes while `server.begin()` happens later inside `initInterfaces()`.
- Added a native HTTP/WebSocket scaffold that keeps the process alive by default and serves a minimal WLED-compatible surface from the shared host runtime entry point.
- Added server-mode tests for default startup, JSON route responses, HTTP state updates, and WebSocket state propagation in `test/native-cli.test.js`.
- Discrepancies or deviations:
- The current server still uses host-side scaffolding rather than the original `ESPAsyncWebServer`-based handlers. That is an intentional interim layer to give the native runtime a long-lived process and browser/API test surface before the original handlers are ported.
- Key decisions and reasoning:
- Any interim native HTTP server should be treated only as scaffolding unless it is wired into the same post-config, post-strip startup stage that the original firmware uses.
- Deferred HTTP-triggered WebSocket broadcast slightly so the server-mode tests observe the same “response first, interface update next” behavior that real WLED approximates via deferred interface updates.
- Verification performed:
- Verified in the original source that `/json`, `/ws`, static assets, `/settings`, `/edit`, upload handling, and live-view routes are all registered before network bind, then activated when interfaces come up.
- Ran `node --test test/native-cli.test.js` outside the sandbox and confirmed server-mode coverage for default start, `/json` routes, HTTP state POSTs, and WebSocket updates.
- Newly discovered tasks or risks:
- A host server that can answer `/json/info` but is not backed by `serializeInfo()`, `serializeState()`, `deserializeState()`, and the strip service loop will diverge from WLED semantics quickly, even if the browser appears to load.
- Browser-visible startup correctness depends on the render loop, presets, playlists, and segment state already existing by the time `/json/si` and `/ws` are first consumed.

### Host networking, Zeroconf, UDP realtime protocols, and time

Objective: replace WiFi/ETH concepts with host networking while preserving WLED protocol behavior and Home Assistant discovery.

Things to accomplish:
- Modify original network-facing files to use host sockets, DNS lookup, multicast/broadcast, and local interface discovery.
- Preserve NTP, node discovery, WLED sync, Hyperion/TPM2.net, E1.31, Art-Net, DDP, realtime UDP behavior, and network-driven state changes where host networking supports them.
- Continue the hardware-removal work for WiFi setup/scanning/AP behavior by exposing host network status and clear UI/API semantics only where useful.
- Implement WLED-compatible Zeroconf/mDNS advertisement for `_wled._tcp.local.` with the HTTP port and `mac` TXT property.
- Ensure the persisted identity from the host runtime foundation matches discovery TXT and `/json/info.mac` across restarts.

Key files:
- `wled00/src/dependencies/network/Network.*`
- `wled00/udp.cpp`
- `wled00/e131.cpp`
- `wled00/ntp.cpp`
- `wled00/wled.cpp`
- `wled00/json.cpp`
- `wled00/data/settings_wifi.htm` and other network-related settings pages
- Host networking utility files under `wled00/` if needed

Dependency-expansion rule:
- If protocol preservation requires changes in state handling, timers, JSON info, UI fields, or tests, expand this area to modify those original files. Do not remove IP control protocols just because their previous transport used ESP classes, and do not reintroduce output buses to preserve old physical-output behavior.

Implementation notes:
- AP mode and WiFi credential provisioning are ESP-only and should already be removed from host UI/API by the hardware-removal area; this area should not bring those controls back.
- Broadcast/multicast behavior must account for hosts with multiple network interfaces and local firewall restrictions.
- Home Assistant compatibility should not depend on RSSI/channel/BSSID fields.
- In the original startup flow, discovery and protocol listeners are not started until after config load and successful network readiness. Native startup should replace WiFi readiness with host interface readiness, then start mDNS, UDP sync sockets, NTP, E1.31, DDP, and similar listeners from one equivalent post-config hook.

Tests and verification:
- Update original tests for any changed JSON info/network fields.
- Add tests for IP parsing, DNS lookup, subnet checks, UDP send/receive, multicast/broadcast where permitted, packet encoders/decoders, NTP/time handling, DDP, E1.31, Art-Net, and WLED notifier fixtures.
- Manual or automated Home Assistant smoke test must confirm `_wled._tcp.local.` advertisement is visible, `mac` TXT equals `/json/info.mac`, `/json` and `/ws` work, and reconnecting after restart does not trigger a MAC mismatch.
- Record firewall or sandbox limitations rather than weakening protocol behavior.

Area log:
- Actions taken:
- Startup analysis: traced `initInterfaces()` and `handleConnection()` in the backup/original source to identify the exact discovery/network services that start after connection state becomes ready.
- Discrepancies or deviations:
- Key decisions and reasoning:
- Home Assistant discovery, notifier UDP, realtime UDP, NTP, E1.31, DDP, Hue reconnect, Alexa init, and OTA all hang off the same post-connection stage in firmware; native startup needs an equivalent “interfaces ready” stage instead of scattering these across ad hoc entry points.
- Verification performed:
- Verified in `wled.cpp` that mDNS publishes both `_http._tcp` and `_wled._tcp` plus the `mac` TXT property, and that UDP notifier, NTP, E1.31, and DDP listeners are started from the same interface-init path.
- Newly discovered tasks or risks:
- Replacing WiFi/AP lifecycle with host networking is not just a transport swap. `handleConnection()` currently decides when `initInterfaces()` runs, when AP fallback is active, when reconnect work happens, and when discovery should be withdrawn or restarted; native startup needs a replacement control flow for those state transitions.
- Startup of MQTT and node-broadcast refresh is loop-driven after interface init, not a one-shot boot action. Native startup must preserve that cadence or provide a host-equivalent scheduler.

### MQTT, Hue, Alexa-compatible behavior, audioreactive, and kept bundled usermods

Objective: keep automation integrations, audio effects, the usermod harness, and only the host-compatible bundled usermods useful on host WLED.

Things to accomplish:
- Modify `wled00/mqtt.cpp`, `hue.cpp`, `alexa.cpp`, bundled dependency wrappers, and usermod manager code to run on host networking/runtime after ESP hardware semantics are removed from the host product path.
- Replace `AsyncMqttClient` with a host MQTT dependency or in-repo implementation while preserving topics, payload parsing, publishing, reconnect behavior, LWT-equivalent behavior, and usermod callbacks.
- Port Hue polling and Alexa-compatible local discovery/control where feasible on host networking.
- Modify `usermods/audioreactive/` to use the OS default microphone/audio input while preserving the existing audioreactive usermod data contract consumed by `FX.cpp`; remove ESP I2S/ADC microphone paths rather than wrapping them in host stubs.
- Preserve FFT, AGC, peak detection, UDP audio sync, and no-microphone disabled/silent behavior using real host audio/input code and tests.
- Preserve `Usermod`, `UsermodManager`, `REGISTER_USERMOD`, usermod config, JSON info/state hooks, MQTT hooks, UDP hooks, and overlay hooks.
- Port only the usermods listed in the keep list in the bundled usermod disposition section. Do not keep removed usermods as disabled source examples, skipped build folders, compatibility shells, or documentation promises.
- Update kept usermods that write pixels directly so they target the normal render-buffer/core-light-engine APIs once physical bus/output semantics are gone.

Key files:
- `wled00/mqtt.cpp`
- `wled00/hue.cpp`
- `wled00/alexa.cpp`
- `wled00/src/dependencies/espalexa/`
- `wled00/src/dependencies/network/`
- `wled00/um_manager.cpp`
- `wled00/fcn_declare.h`
- `usermods/`
- `usermods/audioreactive/audio_reactive.cpp`
- `usermods/audioreactive/audio_source.h`
- `usermods/readme.md`

Dependency-expansion rule:
- If integration or kept-usermod behavior needs additional network, audio, config, UI, JSON, or effect support, expand this area to modify those original files and tests. Do not drop MQTT, Hue/Alexa-compatible behavior, audioreactive data, or the usermod harness to avoid dependency work; remove only usermods that prove to be ESP/GPIO/peripheral-only after deeper review.

Implementation notes:
- Home Assistant's primary WLED integration remains JSON/WebSocket/Zeroconf; MQTT is useful but not a substitute for native WLED integration compatibility.
- Document license/build impact for any selected MQTT/audio library in this plan before merging the dependency.
- Native audio must degrade gracefully when no input device or microphone permission is available.
- Hardware-only usermods are removed by the hardware-cleanup area; if any are rediscovered here, remove them immediately and update the disposition section rather than keeping them as excluded examples.

Tests and verification:
- Update original tests and add basic integration tests with a local MQTT broker for subscribe, command, publish, retain, LWT-equivalent behavior, and usermod callbacks.
- Add synthetic audio tests for FFT/AGC/peak/silence/noise/sine input and verify audio effects consume the usermod data contract.
- Add tests for `setup`, `loop`, config read/write, JSON info/state, lookup behavior, removed-usermod absence, and at least one kept host-ready usermod registration path.
- Manual tests cover Hue polling, Alexa-compatible discovery, microphone permission/no-device fallback, and browser UI settings accuracy where retained.

Area log:
- Actions taken:
  - Plan update: changed this area from broad usermod classification to porting only the explicit keep-list after the hardware-removal area removes unsupported usermods.
  - Plan update: clarified that audioreactive must use real host audio input and remove ESP I2S/ADC paths rather than wrapping them in host stubs.
- Discrepancies or deviations:
  - Earlier plan versions allowed hardware-only usermods to remain as excluded source examples; this now conflicts with the requested native-only cleanup and has been removed.
- Key decisions and reasoning:
  - Preserve the usermod harness and hooks while limiting bundled usermods to host-compatible network, audio, state, timer, and render-buffer behavior.
- Verification performed:
  - Scanned bundled usermods for GPIO, I2C/SPI/UART, ESP-only headers, sensor/display libraries, output-driver libraries, filesystem use, and network use before adding the keep/remove disposition.
- Newly discovered tasks or risks:

### Native CI, packaging, install/service helpers, and final repository transition

Objective: finish the repository transition to a host WLED application with documented build, test, install, and service workflows.

Things to accomplish:
- Replace firmware-centered CI with macOS/Linux host build, test, browser, and integration checks as part of the repository transition; do not retain firmware CI as a native fallback.
- Remove or archive `platformio.ini`, `platformio_override.sample.ini`, ESP-only board JSON files, firmware-only partition CSV files, `pio-scripts/`, and firmware-only requirements as part of the native repository transition. Do not retain them as fallback validation for native work; ensure replacement host CI/docs/tests are updated in the same change.
- Keep or adapt Node web UI build/test tooling for host asset packaging.
- Add install/start/stop/status/uninstall helpers, Linux systemd user service templates, macOS launchd plist generation/install helpers, and packaging notes for tarball/zip plus optional Homebrew/deb/rpm future work.
- Document firewall, ports, config paths, logs, service management, migration/import, unsupported ESP hardware behavior, and update/install flow in existing docs (`readme.md`, `AGENTS.md`, and this plan), not in separate native status docs.

Key files:
- `.github/workflows/`
- `.gitignore`
- `package.json`
- `requirements.txt`
- `readme.md`
- `AGENTS.md`
- `CMakeLists.txt` or selected host build files
- `scripts/`
- `platformio.ini`
- `pio-scripts/`
- `boards/`
- `tools/*partition*.csv`
- Service/packaging files if added
- `Native-Port-Plan.md`

Dependency-expansion rule:
- If final CI or packaging reveals missing validation coverage, installation behavior, docs, or cleanup dependencies, expand this area to implement them before deleting firmware-era tooling.

Implementation notes:
- Default service should run as the current user unless explicitly configured otherwise.
- Service helpers must not overwrite config without confirmation.
- Expose port binding and config path through environment or command-line flags.
- No docs should point developers to PlatformIO as the required workflow after this area is completed.

Tests and verification:
- CI runs Linux host build/test and macOS host build/test if available.
- CI runs web build/tests, host unit tests, protocol fixture tests, browser smoke tests, and Home Assistant compatibility smoke/manual documentation.
- Linux install/start/status/stop/uninstall service flow works in a clean user environment.
- macOS launchd load/start/stop/unload flow works.
- App restarts with the same config and serves the UI after reboot/login according to selected service mode.
- `git diff --check` and documentation link checks pass.

Area log:
- Actions taken:
  - Plan update: removed wording that kept firmware tooling until replacement CI existed and reframed final cleanup as a native repository transition without firmware fallback validation.
  - Removed `platformio.ini`, `platformio_override.sample.ini`, `requirements.in`, `requirements.txt`, `pio-scripts/`, `boards/`, `.github/platformio_release.ini.template`, and the firmware-centric GitHub workflows.
  - Replaced the previous workflow chain with a single native CI workflow in `.github/workflows/wled-ci.yml` that runs `npm run build`, `npm test`, `scripts/native-build.sh`, and `scripts/native-test.sh` on Linux and macOS.
  - Updated contributor-facing docs and added `test/native-repo-cleanup.test.js` to guard the removed PlatformIO-era surface in core workflow files.
- Discrepancies or deviations:
  - Earlier plan versions treated PlatformIO/ESP tooling as late temporary infrastructure; this now conflicts with the native-only direction once host workflow changes are made.
  - Release/nightly firmware packaging workflows were deleted instead of being replaced immediately because the repository does not yet have a native packaging pipeline worth codifying.
- Key decisions and reasoning:
  - Native CI, packaging, and docs should replace firmware workflow directly rather than leaving PlatformIO as the required or fallback path.
  - It is better to remove dead firmware-era automation now and add native packaging later than to keep misleading release/build paths that no longer correspond to the product direction.
- Verification performed:
  - Confirmed the prior version of this plan still used numbered sequential sections, and removed that structure as part of this reorganization.
  - Added a repository test that asserts the main PlatformIO artifacts are gone and that core workflow docs no longer prescribe the old firmware toolchain.
- Newly discovered tasks or risks:
  - Many usermod readmes and historical notes still mention firmware-era configuration files. Those are no longer active build entry points, but they will need broader cleanup as bundled usermods are ported or removed.
