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

- `AGENTS.md`, `.github/copilot-instructions.md`, and `.github/agent-build.instructions.md` currently describe a PlatformIO/Arduino firmware workflow. During the transition, they must point contributors back to this plan rather than to a separate native-porting document.
- `readme.md` describes WLED as ESP32/ESP8266 firmware and lists ESP-specific features such as access point mode, OTA, and compatible hardware. Native user-visible behavior changes must be reflected there when implementation tasks make them real.
- `platformio.ini`, `pio-scripts/`, `boards/`, `requirements.txt`, and GitHub workflows are centered on ESP firmware environments.
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

## Plan Maintenance Rules For Every Task

Each task below is sequential and should leave WLED in a more functional state than before. Before finishing any task:

1. Update that task's log in this file with actions taken, deviations, key decisions, verification performed, and newly discovered tasks or risks.
2. If execution reveals a missing dependency that blocks a host-feasible feature, expand the current task to modify additional original WLED files, shared utilities, tests, or build rules. If the dependency is ESP/GPIO/peripheral-only, remove that source path instead of preserving it as a blocker, placeholder, or compatibility shell.
3. Update `readme.md`, `AGENTS.md`, `.github/` instructions, or other existing contributor docs only when commands, supported platforms, configuration paths, build systems, or user-visible behavior actually change. Do not create separate native-port status documents.
4. Update original tests as needed and add basic new tests in the same normal test flow. If a new test runner is introduced, wire it into the repository scripts/CI so it does not become ignored side coverage.
5. Run the meaningful validation loop available at that stage and record failures that are environmental, incomplete by design, or intentionally deferred.

## Task 1: Baseline, Existing Backup, Scope, And Test Inventory

Objective: establish a safe in-place migration baseline before code churn starts.

Scope:
- Use the existing recursive `wled00-backup/` snapshot only for comparison and recovery during migration. It is reference-only and must not become a second port or be refreshed into an alternate source tree.
- Confirm the branch migration goal: modify `wled00/` in place until it builds and runs as host WLED.
- Inventory current tests, web build outputs, firmware build expectations, and any existing generated artifacts.
- Record the feature matrix and first user-visible breaking changes in this file.
- Remove wording that points to `docs/native-porting.md`, `docs/native-running.md`, or any separate native status file.

Key files:
- `Native-Port-Plan.md`
- `wled00/`
- `wled00-backup/` (existing reference-only snapshot)
- `readme.md`
- `AGENTS.md`
- `package.json`
- `tools/cdata-test.js`
- `test/`

Dependency-expansion rule:
- If baseline work reveals missing migration prerequisites, expand this task to update the affected original docs, build scripts, or tests rather than postponing decisions into an undocumented side plan.

Implementation notes:
- Keep original `wled00/` as the only source tree that future tasks compile and run.
- Do not recreate top-level `native/` for host product code; use `wled00/` for WLED runtime/source edits and shared repo locations only for build scripts, tests, CI, or packaging glue.
- Capture current generated-header behavior so later web UI changes continue to rebuild through the existing tooling.

Tests and verification:
- Run `npm test` as the current baseline.
- Run `npm run build` if web tooling or asset generation documentation changes.
- If PlatformIO is available, run `pio run -e esp32dev` before major source edits to capture the starting firmware state; if unavailable, record that limitation.

Task log:
- Actions taken:
  - Confirmed the branch target remains in-place migration of `wled00/` and that `wled00-backup/` is present only as an ignored reference snapshot.
  - Inventoried the current automated test surface: `npm test` runs the Node.js test runner, `tools/cdata-test.js` validates web asset generation, `test/` only contained the PlatformIO README baseline before native task work, and generated web headers remain ignored artifacts under `wled00/`.
  - Added a native CLI smoke test entry under `test/native-cli.test.js` so the normal `npm test` flow now covers task 2's host bootstrap path.
  - Updated contributor-facing docs (`readme.md`, `AGENTS.md`, `.github/copilot-instructions.md`, `.github/agent-build.instructions.md`) to point native-port work back to this plan and the new `scripts/native-*.sh` wrappers instead of any separate native status document.
- Discrepancies or deviations:
  - The repository still contains PlatformIO firmware tooling and workflows because task 1 only establishes the baseline; removal remains a later plan item after host replacements are expanded.
- Key decisions and reasoning:
  - Kept task 1 changes focused on inventory and documentation because the repo already did not reference `docs/native-porting.md` or `docs/native-running.md`; the missing baseline gap was explicit native workflow/test inventory rather than stale links.
  - Reused the existing Node.js test runner for the new native smoke test so native validation enters the normal repository test flow immediately instead of creating an isolated side harness.
- Verification performed:
  - Ran `node --test test/native-cli.test.js` before implementation and confirmed failure because `scripts/native-run.sh` did not yet exist.
  - Ran `npm ci`, `npm test`, and `npm run build` after the task 1/task 2 changes landed.
  - Attempted `pio run -e esp32dev`, but `pio` is not installed in this environment (`command not found`), so firmware-baseline verification remains pending on a PlatformIO-capable machine.
- Newly discovered tasks or risks:
  - Host runtime work in task 3 will need a reusable way to expose the package version/build metadata to more of `wled00/` than the standalone bootstrap CLI currently touches.

## Task 2: Host Build Entry Point For The Original `wled00` Tree

Objective: introduce a host build/run path that compiles source from `wled00/` rather than a separate native application tree.

Scope:
- Add a host build system such as CMake or another local build entry point that targets original `wled00` sources.
- Add a minimal host executable entry point in or directly beside `wled00/` that reports WLED version/help and prepares to call WLED setup/loop code.
- Add scripts such as `scripts/native-build.sh`, `scripts/native-run.sh`, and `scripts/native-test.sh` only as wrappers around the in-place `wled00` build.
- Add `--help`, `--config-dir`, `--host`, `--port`, `--log-level`, and `--version` CLI handling early enough that later tasks use stable commands.
- Keep Node web build/test working during this transition.
- Record any chosen build dependency, license, packaging impact, and boundary in this file.

Key files:
- `CMakeLists.txt` or selected host build files
- `wled00/` build entry source, for example a host-specific main under `wled00/`
- `scripts/native-build.sh`
- `scripts/native-run.sh`
- `scripts/native-test.sh`
- `package.json`
- `.github/workflows/`
- `Native-Port-Plan.md`

Dependency-expansion rule:
- If the host entry point cannot compile without small foundational edits, expand this task to adjust the necessary original `wled00` includes, macros, or build guards. Do not create a separate source copy that avoids the original files.

Implementation notes:
- Build artifacts should go under ignored build directories such as `build/native/`, but source should remain in `wled00/` or shared repository build/script locations.
- The executable may be named `wled-native` as a build artifact, but help/version output should identify the product as WLED.

Tests and verification:
- Update `package.json` or existing scripts so host build smoke tests are part of the normal developer flow when available.
- Add a basic CLI smoke test for `--help` and `--version`.
- Run `npm test`, `npm run build`, `scripts/native-build.sh`, and `scripts/native-run.sh --help`.

Task log:
- Actions taken:
  - Added a root `CMakeLists.txt` that extracts the version from `package.json` and builds a `wled-native` executable from source files living under `wled00/`.
  - Added `wled00/wled_host_cli.h`, `wled00/wled_host_cli.cpp`, and `wled00/wled_host_main.cpp` as the first host-only bootstrap entry point for the original source tree.
  - Added `scripts/native-build.sh`, `scripts/native-run.sh`, and `scripts/native-test.sh` as thin wrappers around the in-place host build and CLI smoke path.
  - Added `native:build`, `native:run`, and `native:test` npm scripts so the host workflow is discoverable through the existing developer tooling.
- Discrepancies or deviations:
  - The bootstrap executable currently parses and reports CLI options but does not call `WLED::instance().setup()` / `loop()` yet; that work is intentionally deferred to task 3 because the current `wled00` runtime still hard-depends on Arduino/ESP headers and lifecycle assumptions.
- Key decisions and reasoning:
  - Used CMake because it is already available on macOS/Linux developer machines and can target original `wled00/` sources without introducing a second application tree.
  - Kept the first native compile target intentionally small so task 2 establishes a stable command-line contract (`--help`, `--version`, `--config-dir`, `--host`, `--port`, `--log-level`) before broader de-ESP runtime work begins.
  - Parsed the package version directly from `package.json` during CMake configure so the host CLI reports the same version string as the existing web build tooling without a new duplicate version file.
  - Chose CMake as a build-time-only dependency boundary for the host path; it adds no runtime dependency to the produced binary, and current packaging impact is limited to requiring CMake plus a C++17 compiler on developer/build machines.
- Verification performed:
  - Added `test/native-cli.test.js` first, then confirmed the red failure before writing the host scaffold.
  - Ran `node --test test/native-cli.test.js`, `npm test`, `npm run build`, `scripts/native-build.sh`, `scripts/native-run.sh --help`, and `scripts/native-test.sh` successfully after implementation.
- Newly discovered tasks or risks:
  - Task 3 needs a shared host-facing configuration/bootstrap layer so future native binaries, tests, and runtime code do not duplicate CLI parsing or config-root resolution logic.

## Task 3: Directly De-ESP Foundation, Filesystem, Config, Identity, And Runtime

Objective: make original WLED startup, timing, filesystem, configuration, identity, logging, and shutdown work on macOS/Linux.

Scope:
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
- If startup/config work requires additional WLED globals, JSON helpers, or filesystem callers to change, expand this task to modify those original files. Do not skip configuration, presets, or identity because a helper is missing.

Implementation notes:
- Reject path traversal and absolute-path escapes when serving or writing files through WLED APIs.
- Replace ESP watchdog/heap-panic behavior with host diagnostics and clean exit/restart request semantics.
- Avoid busy loops that burn CPU on host systems.
- Keep secrets such as `wsec.json` inside the config root, not generated build directories.

Tests and verification:
- Update existing tests for any changed web asset/config assumptions.
- Add basic tests for config root selection, default file creation, read/write/delete/list/copy behavior, path traversal rejection, persisted identity stability, and graceful shutdown/restart code.
- Run host CLI/runtime tests through the repository test scripts, plus `npm test` and `npm run build` when relevant.

Task log:
- Actions taken:
- Discrepancies or deviations:
- Key decisions and reasoning:
- Verification performed:
- Newly discovered tasks or risks:

## Task 4: Remove ESP Hardware Semantics, Unsupported Usermods, UI/API Fields, And Output Drivers

Objective: remove host-impossible ESP hardware paths immediately after the host runtime foundation so later porting work cannot depend on ESP fallbacks.

Scope:
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

Task log:
- Actions taken:
  - Plan update: moved ESP hardware cleanup from former Task 8 to Task 4, immediately after the host runtime foundation.
  - Plan update: added unsupported bundled usermod removal to this task so hardware-only usermods cannot remain as native build/documentation baggage.
- Discrepancies or deviations:
  - Earlier plan deferred cleanup until host equivalents were functional; this now conflicts with the native-only direction and has been replaced with early removal of ESP/GPIO/peripheral paths.
- Key decisions and reasoning:
  - Remove physical output drivers, GPIO semantics, ESP board fields, and unsupported usermods before rendering/network/integration work so later tasks cannot accidentally preserve ESP-only abstractions.
- Verification performed:
  - Reviewed task ordering and searched this plan for old late-cleanup and stub/null-output wording.
- Newly discovered tasks or risks:

## Task 5: Render Buffer, Core Light Engine, Effects, Presets, Playlists, And Timers

Objective: bring up WLED's core light engine in the original source tree with the render buffer as application state, not as a physical/null/virtual output path.

Scope:
- Modify `wled00/bus_manager.*`, `bus_wrapper.h`, `FX*.cpp`, `FX*.h`, `colors.*`, `palettes.cpp`, `led.cpp`, `playlist.cpp`, `presets.cpp`, `json.cpp`, and related files so host systems render into WLED's internal frame/state buffer without ESP physical output buses.
- Preserve segment rendering, color order, RGBW/CCT metadata where meaningful, transitions, presets, playlists, nightlight, timers, palettes, 1D effects, and 2D effects that can run without unavailable hardware. Keep `BusManager` only if it is slimmed to render-buffer/core-light-engine coordination; remove ESP bus subclasses and output-driver semantics.
- Remove NeoPixelBus/RMT/I2S/LEDC/HUB75/MY92xx/PWM physical output dependencies from the host build. Do not add `BusNull`, virtual outputs, stub outputs, preview outputs, GPIO-backed outputs, or other interim output backends.
- Keep the normal render buffer as the source for existing live-view/peek behavior; do not add a separate browser preview feature.
- Keep network realtime protocols out of this task except where core state hooks are needed; Task 7 handles host networking/control protocols and must not reintroduce output buses.

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
- If effect compilation reveals missing timers, random sources, palette storage, JSON state helpers, or render dependencies, expand this task to modify those original WLED files. Do not replace full host-feasible effects with permanent placeholders; remove only effects or code paths that are intrinsically tied to removed hardware.

Implementation notes:
- Treat compile failures in hot-path code as portability work, not reasons to drop effects.
- Replace ESP-specific performance attributes with empty or compiler-appropriate host attributes where needed.
- Preserve deterministic behavior for tests by adding injectable clock/random hooks only where needed and keeping production behavior WLED-compatible.
- Do not plan future output backends in this task; this migration is focused on removing ESP-only outputs and preserving the host render state.

Tests and verification:
- Update original tests affected by effect/palette/config serialization changes.
- Add basic host tests for color conversion, configured render length, color order, RGBW/CCT metadata, selected effects rendering non-empty frames into the render buffer, preset save/apply, playlist advancement, nightlight fade, and timer behavior.
- Test that `/ws` live-view frames, when retained, read from the normal render buffer.
- Run host build/tests plus `npm test`; run browser checks after Task 6 makes the server available.

Task log:
- Actions taken:
  - Plan update: removed memory/null/no-op output backend language and reframed the render buffer as core WLED application state.
- Discrepancies or deviations:
  - Earlier plan allowed a null output backend as a first milestone; this was removed because it could become an interim output architecture rather than native cleanup.
- Key decisions and reasoning:
  - Keep effects, presets, playlists, timers, segments, and live-view compatibility by rendering into the normal buffer, while deleting ESP bus/output subclasses instead of replacing them with virtual outputs.
- Verification performed:
  - Checked cross-task references so HTTP/browser validation now points to Task 6 and networking/control protocols point to Task 7.
- Newly discovered tasks or risks:

## Task 6: HTTP Server, WebSocket Server, Web UI, And JSON API

Objective: serve the existing WLED browser UI and API from the host process using original WLED handlers as much as possible.

Scope:
- Modify `wled00/wled_server.cpp`, `ws.cpp`, `json.cpp`, `xml.cpp`, `set.cpp`, and related request/response code to use host HTTP/WebSocket primitives.
- Replace `ESPAsyncWebServer`, `AsyncWebSocket`, `AsyncCallbackJsonWebHandler`, and response/request classes by editing original server code and introducing narrow host request/response utilities only where shared behavior is needed.
- Serve existing `wled00/data/` assets or generated assets according to the selected build mode.
- Preserve `/`, `/settings`, `/json`, `/json/state`, `/json/info`, `/json/si`, `/ws`, `/edit`, `/upload`, `/version`, `/uptime`, palette routes, live view routes, and static JS/CSS routes where applicable. Keep `/freeheap` only if Task 4 gives it documented host memory semantics; otherwise remove or replace it with a documented host diagnostics route.
- Ensure OTA/bootloader update endpoints removed in Task 4 stay removed or report documented host package-update behavior; they must not imply ESP firmware flashing exists.
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
- If preserving a host-feasible route requires more state, filesystem, segment, preset, playlist, or render support, expand this task to wire those dependencies into the original handlers. Do not duplicate a simplified API elsewhere; remove routes only when Task 4 classified their behavior as ESP-only and document the breaking change.

Implementation notes:
- Preserve Home Assistant's WebSocket expectations: connect to `/ws`, receive state/info on connect and state changes, accept JSON state updates, and support verbose replies.
- Browser testing is mandatory for changed pages.
- Existing live-view/peek compatibility remains allowed only as a view into the normal render buffer.

Tests and verification:
- Update `tools/cdata-test.js` and existing web build tests when asset packaging changes.
- Add HTTP/API tests that start host WLED on a random local port, query JSON routes, POST state changes, observe WebSocket output, and verify render-buffer changes.
- Browser smoke tests must cover page load, console errors, navigation, color controls, effect/palette changes, settings pages, upload/list editor paths, WebSocket updates, live-view if retained, and mobile-sized viewport smoke checks.
- Run `npm run build`, `npm test`, host tests, and browser tests.

Task log:
- Actions taken:
- Discrepancies or deviations:
- Key decisions and reasoning:
- Verification performed:
- Newly discovered tasks or risks:

## Task 7: Host Networking, Zeroconf, UDP Realtime Protocols, And Time

Objective: replace WiFi/ETH concepts with host networking while preserving WLED protocol behavior and Home Assistant discovery.

Scope:
- Modify original network-facing files to use host sockets, DNS lookup, multicast/broadcast, and local interface discovery.
- Preserve NTP, node discovery, WLED sync, Hyperion/TPM2.net, E1.31, Art-Net, DDP, realtime UDP behavior, and network-driven state changes where host networking supports them.
- Continue the Task 4 removal of WiFi setup/scanning/AP behavior by exposing host network status and clear UI/API semantics only where useful.
- Implement WLED-compatible Zeroconf/mDNS advertisement for `_wled._tcp.local.` with the HTTP port and `mac` TXT property.
- Ensure the generated persisted identity from Task 3 matches discovery TXT and `/json/info.mac` across restarts.

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
- If protocol preservation requires changes in state handling, timers, JSON info, UI fields, or tests, expand this task to modify those original files. Do not remove IP control protocols just because their previous transport used ESP classes, and do not reintroduce output buses to preserve old physical-output behavior.

Implementation notes:
- AP mode and WiFi credential provisioning are ESP-only and should already be removed from host UI/API by Task 4; this task should not bring those controls back.
- Broadcast/multicast behavior must account for hosts with multiple network interfaces and local firewall restrictions.
- Home Assistant compatibility should not depend on RSSI/channel/BSSID fields.

Tests and verification:
- Update original tests for any changed JSON info/network fields.
- Add tests for IP parsing, DNS lookup, subnet checks, UDP send/receive, multicast/broadcast where permitted, packet encoders/decoders, NTP/time handling, DDP, E1.31, Art-Net, and WLED notifier fixtures.
- Manual or automated Home Assistant smoke test must confirm `_wled._tcp.local.` advertisement is visible, `mac` TXT equals `/json/info.mac`, `/json` and `/ws` work, and reconnecting after restart does not trigger a MAC mismatch.
- Record firewall or sandbox limitations rather than weakening protocol behavior.

Task log:
- Actions taken:
- Discrepancies or deviations:
- Key decisions and reasoning:
- Verification performed:
- Newly discovered tasks or risks:

## Task 8: MQTT, Hue, Alexa-Compatible Behavior, Audioreactive, And Kept Usermods

Objective: keep automation integrations, audio effects, the usermod harness, and only the host-compatible bundled usermods useful on host WLED.

Scope:
- Modify `wled00/mqtt.cpp`, `hue.cpp`, `alexa.cpp`, bundled dependency wrappers, and usermod manager code to run on host networking/runtime after Task 4 has removed ESP hardware semantics.
- Replace `AsyncMqttClient` with a host MQTT dependency or in-repo implementation while preserving topics, payload parsing, publishing, reconnect behavior, LWT-equivalent behavior, and usermod callbacks.
- Port Hue polling and Alexa-compatible local discovery/control where feasible on host networking.
- Modify `usermods/audioreactive/` to use the OS default microphone/audio input while preserving the existing audioreactive usermod data contract consumed by `FX.cpp`; remove ESP I2S/ADC microphone paths rather than wrapping them in host stubs.
- Preserve FFT, AGC, peak detection, UDP audio sync, and no-microphone disabled/silent behavior using real host audio/input code and tests.
- Preserve `Usermod`, `UsermodManager`, `REGISTER_USERMOD`, usermod config, JSON info/state hooks, MQTT hooks, UDP hooks, and overlay hooks.
- Port only the usermods listed in the keep list in the bundled usermod disposition section. Do not keep removed usermods as disabled source examples, skipped build folders, compatibility shells, or documentation promises.
- Update kept usermods that write pixels directly so they target the normal render-buffer/core-light-engine APIs after Task 5 removes physical bus/output semantics.

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
- If integration or kept-usermod behavior needs additional network, audio, config, UI, JSON, or effect support, expand this task to modify those original files and tests. Do not drop MQTT, Hue/Alexa-compatible behavior, audioreactive data, or the usermod harness to avoid dependency work; remove only usermods that prove to be ESP/GPIO/peripheral-only after deeper review.

Implementation notes:
- Home Assistant's primary WLED integration remains JSON/WebSocket/Zeroconf; MQTT is useful but not a substitute for native WLED integration compatibility.
- Document license/build impact for any selected MQTT/audio library in this plan before merging the dependency.
- Native audio must degrade gracefully when no input device or microphone permission is available.
- Hardware-only usermods are removed by Task 4; if any are rediscovered here, remove them immediately and update the disposition section rather than keeping them as excluded examples.

Tests and verification:
- Update original tests and add basic integration tests with a local MQTT broker for subscribe, command, publish, retain, LWT-equivalent behavior, and usermod callbacks.
- Add synthetic audio tests for FFT/AGC/peak/silence/noise/sine input and verify audio effects consume the usermod data contract.
- Add tests for `setup`, `loop`, config read/write, JSON info/state, lookup behavior, removed-usermod absence, and at least one kept host-ready usermod registration path.
- Manual tests cover Hue polling, Alexa-compatible discovery, microphone permission/no-device fallback, and browser UI settings accuracy where retained.

Task log:
- Actions taken:
  - Plan update: changed this task from broad usermod classification to porting only the explicit keep-list after Task 4 removes unsupported usermods.
  - Plan update: clarified that audioreactive must use real host audio input and remove ESP I2S/ADC paths rather than wrapping them in host stubs.
- Discrepancies or deviations:
  - Earlier plan allowed hardware-only usermods to remain as excluded source examples; this now conflicts with the requested native-only cleanup and has been removed.
- Key decisions and reasoning:
  - Preserve the usermod harness and hooks while limiting bundled usermods to host-compatible network, audio, state, timer, and render-buffer behavior.
- Verification performed:
  - Scanned bundled usermods for GPIO, I2C/SPI/UART, ESP-only headers, sensor/display libraries, output-driver libraries, filesystem use, and network use before adding the keep/remove disposition.
- Newly discovered tasks or risks:

## Task 9: Native CI, Packaging, Install/Service Helpers, And Final Repository Transition

Objective: finish the repository transition to a host WLED application with documented build, test, install, and service workflows.

Scope:
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
- If final CI or packaging reveals missing validation coverage, installation behavior, docs, or cleanup dependencies, expand this task to implement them before deleting firmware-era tooling.

Implementation notes:
- Default service should run as the current user unless explicitly configured otherwise.
- Service helpers must not overwrite config without confirmation.
- Expose port binding and config path through environment or command-line flags.
- No docs should point developers to PlatformIO as the required workflow after this task.

Tests and verification:
- CI runs Linux host build/test and macOS host build/test if available.
- CI runs web build/tests, host unit tests, protocol fixture tests, browser smoke tests, and Home Assistant compatibility smoke/manual documentation.
- Linux install/start/status/stop/uninstall service flow works in a clean user environment.
- macOS launchd load/start/stop/unload flow works.
- App restarts with the same config and serves the UI after reboot/login according to selected service mode.
- `git diff --check` and documentation link checks pass.

Task log:
- Actions taken:
  - Plan update: removed wording that kept firmware tooling until replacement CI existed and reframed final cleanup as a native repository transition without firmware fallback validation.
- Discrepancies or deviations:
  - Earlier plan treated PlatformIO/ESP tooling as late temporary infrastructure; this now conflicts with the native-only direction once host workflow changes are made.
- Key decisions and reasoning:
  - Native CI, packaging, and docs should replace firmware workflow directly rather than leaving PlatformIO as the required or fallback path.
- Verification performed:
  - Confirmed task headings remain sequential from Task 1 through Task 9 after moving cleanup earlier.
- Newly discovered tasks or risks:
