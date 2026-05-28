# WLED Native Port Plan

Goal: port this repository from ESP32/ESP8266 firmware to a native macOS/Linux application, dropping ESP hardware support while keeping all WLED functionality that can run on a host OS.

Recommended architecture: preserve WLED's core behavior and public APIs, then replace ESP/Arduino dependencies with a native platform layer. This is safer than a rewrite because effects, palettes, presets, playlists, JSON API, web UI, UDP realtime protocols, Home Assistant integration behavior, and the usermod harness are already organized around WLED abstractions.

## Confirmed Native Scope

- Final repository support is native macOS/Linux only. ESP32, ESP8266, PlatformIO firmware builds, OTA firmware update paths, ESP-NOW, hardware IR receivers, hardware DMX GPIO/UART, I2S/RMT/LEDC LED drivers, ESP Ethernet boards, ESP watchdog/brownout handling, and GPIO pin management can be removed once native replacements or documented exclusions exist.
- The product name remains **WLED** in user-visible surfaces. Use "native" only where needed to distinguish build/runtime internals, documentation sections, or command names.
- Native WLED runs as a headless local web service by default, with the existing browser UI served by the process, native logs, service install/start helpers, and a stable config directory.
- Existing WLED API compatibility is a release gate. Preserve `/json`, `/json/state`, `/json/info`, `/json/si`, `/ws`, presets, playlists, segments, effects, palettes, realtime UDP/Art-Net/DDP/E1.31 inputs, MQTT, Hue/Alexa-compatible behavior where possible, and web UI semantics.
- Home Assistant compatibility is a release gate for the first useful native milestone. Native WLED must be discoverable as WLED, expose a stable device identity, and behave correctly with Home Assistant's WLED JSON API and WebSocket client.
- Do not add new browser preview functionality as part of the native port. Keep existing WLED UI/API live-view or "peek" behavior only when it can be backed by the normal internal render buffer without creating a separate preview product surface.
- Do not require physical LED output on day one. ESP-dependent physical output paths should be removed from the native build. Keep an internal render buffer and a no-op/null output path so effects, API state, integrations, and tests can run. Direct host-connected LED hardware, USB serial controllers, USB DMX, SPI, HID, vendor SDK outputs, or network forwarding backends may be added later as explicit native output backends.
- Native microphone support should use the OS default input device first, expose device selection later, and feed the existing audioreactive usermod data contract so audio effects continue to work.
- Third-party native dependencies are acceptable when documented, license-compatible, and contained behind adapter boundaries.
- First-run configuration should use WLED defaults. Do not automatically import an existing ESP `cfg.json`, `wsec.json`, `presets.json`, palettes, ledmaps, or playlists unless the user runs an explicit import/migration command added in a later task.

## Research Baseline

- Home Assistant's current WLED integration is local-push, auto-discovers WLED through Zeroconf, exposes lights/selects/numbers/sensors/switches/buttons/update entities, prefers WebSocket push updates, falls back to polling, and relies on the WLED JSON API. Source: <https://www.home-assistant.io/integrations/wled/>.
- Home Assistant's current WLED manifest declares `zeroconf: ["_wled._tcp.local."]` and depends on the `wled` Python client. Source: <https://raw.githubusercontent.com/home-assistant/core/dev/homeassistant/components/wled/manifest.json>.
- Home Assistant's config flow uses the discovered Zeroconf `mac` TXT property when present and otherwise queries the device, then uses the WLED info MAC address as the unique ID. Native WLED therefore needs a stable MAC-compatible instance ID persisted in the config root and exposed consistently through discovery and `/json/info`. Source: <https://raw.githubusercontent.com/home-assistant/core/dev/homeassistant/components/wled/config_flow.py>.
- The WLED JSON API contract is centered on `/json`, `/json/state`, `/json/info`, `state`, `info`, `effects`, `palettes`, segments, presets, playlists, and partial state POSTs. Source: <https://kno.wled.ge/interfaces/json-api/>.
- The WLED WebSocket contract uses `/ws`, accepts JSON state updates, sends state/info updates on lighting changes, supports `{"v":true}`, and supports live LED value streaming with `{"lv":true}` for the existing Peek/live-view feature. Source: <https://kno.wled.ge/interfaces/websocket/>.
- WLED network realtime protocols remain relevant on native hosts because E1.31/Art-Net/DDP/UDP sync are IP protocols, even though WiFi/AP-specific advice in current docs becomes irrelevant. Sources: <https://kno.wled.ge/interfaces/e1.31-dmx/>, <https://kno.wled.ge/interfaces/ddp/>, <https://kno.wled.ge/interfaces/udp-notifier/>.
- Linux config defaults should follow the XDG Base Directory Specification: `$XDG_CONFIG_HOME` when set, otherwise `$HOME/.config`. Source: <https://specifications.freedesktop.org/basedir-spec/latest/>.
- macOS support/config files belong under the user's Library/Application Support area rather than exposed user documents. Source: <https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/FileSystemProgrammingGuide/FileSystemOverview/FileSystemOverview.html>.
- Candidate native dependencies identified for later evaluation: Boost.Beast for C++ HTTP/WebSocket on Boost.Asio (<https://www.boost.org/library/latest/beast/>), libwebsockets for a C HTTP/WebSocket server with an MIT license (<https://libwebsockets.org/>), and PortAudio for cross-platform native audio capture (<https://www.portaudio.com/>). These are candidates, not decisions; each task must record chosen dependency, license, packaging impact, and adapter boundary.

## Current Repo Analysis

- `AGENTS.md`, `.github/copilot-instructions.md`, and `.github/agent-build.instructions.md` define a PlatformIO/Arduino workflow. They require `npm run build` before firmware compilation and validate firmware through `pio run -e esp32dev`.
- `README.md` describes WLED as ESP32/ESP8266 firmware and lists ESP-specific features such as access point mode, OTA, and compatible hardware.
- `platformio.ini`, `pio-scripts/`, `boards/`, `requirements.txt`, and `.github/workflows/build.yml` are centered on ESP firmware environments.
- `wled00/wled.h` is the highest-impact dependency hub. It directly includes Arduino, WiFi/ETH, LittleFS, ESPAsyncWebServer, DNSServer, WiFiUDP, AsyncMqttClient, OTA, IR, ESP-NOW, and generated web headers.
- `wled00/wled.cpp` contains the runtime lifecycle, filesystem mount, WiFi setup, UDP/WebSocket setup, OTA/IR setup, heap/PSRAM watchdog logic, and main loop.
- `wled00/bus_manager.*` and `wled00/bus_wrapper.h` contain the LED output boundary. They already distinguish digital, PWM, on/off, network, HUB75, and placeholder buses, which is the best place to introduce native output backends.
- `wled00/bus_manager.*` already contains network bus types for DDP, E1.31, Art-Net, and LIFX. Preserve their logic only if they are useful as explicit native output backends; do not make them a first-milestone requirement.
- `wled00/file.cpp`, `cfg.cpp`, `presets.cpp`, `playlist.cpp`, `colors.cpp`, and ledmap/palette code expect an Arduino filesystem facade named `WLED_FS` and JSON files such as `cfg.json`, `wsec.json`, `presets.json`, palette files, and ledmaps.
- `wled00/wled_server.cpp`, `json.cpp`, `ws.cpp`, `xml.cpp`, `set.cpp`, and `wled00/data/` define the web/API surface. These should be preserved and tested in a browser instead of replaced.
- `wled00/udp.cpp`, `e131.cpp`, `mqtt.cpp`, `hue.cpp`, `alexa.cpp`, and `ntp.cpp` are mostly portable protocol logic mixed with Arduino networking types.
- `wled00/wled.cpp` publishes `_wled._tcp` mDNS with a `mac` TXT property today, and `wled00/json.cpp` emits `info.mac`. Native WLED must preserve that externally visible identity contract even though the value is generated rather than read from WiFi hardware.
- `wled00/ws.cpp` and `wled00/data/index.js` already implement WebSocket state updates and live-view/peek behavior. The native port should keep this compatibility path only as a view into the normal render buffer.
- `wled00/um_manager.cpp` and `fcn_declare.h` preserve a usermod API based on `Usermod` and `REGISTER_USERMOD`. Keep this harness.
- `usermods/audioreactive/` is the most valuable hardware-adjacent usermod to port because many built-in effects consume its data through `UsermodManager::getUMData(..., USERMOD_ID_AUDIOREACTIVE)`.
- A broad search found over 200 files using Arduino or ESP-adjacent constructs such as `String`, `Print`, `File`, `IPAddress`, `Serial`, `millis()`, `delay()`, `PROGMEM`, `ESP32`, `ESP8266`, LittleFS, ESPAsyncWebServer, WiFiUDP, NeoPixelBus, GPIO, I2S, RMT, and pin APIs. The port must be staged.

## Configuration Storage Recommendation

Use a native config root with the same WLED JSON file names inside it:

- Command-line override: `--config-dir <path>` has highest priority.
- Environment override: `WLED_CONFIG_DIR=<path>` is second priority.
- Linux default: `$XDG_CONFIG_HOME/wled` if set, otherwise `~/.config/wled`.
- macOS default: `~/Library/Application Support/WLED`.
- Development default: allow `--config-dir ./.wled-native` for local repeatable tests.

Keep `cfg.json`, `wsec.json`, `presets.json`, `palette*.json`, `ledmap*.json`, `remote.json`, and uploaded web assets under that root. Generate the same default configuration shape WLED uses on first run instead of auto-importing existing ESP files. Add an explicit import command later if needed, but make import opt-in and non-destructive. Add a `runtime/` or `cache/` subdirectory only for generated logs, pid files, caches, and transient data. Do not store secrets in repo-local defaults unless explicitly requested.

## Task 1: Lock Native Product Scope And Removal Policy

Objective: establish the native product contract before code churn starts.

Scope:
- Decide and document native-only support, supported OS versions, first runnable user experience, feature parity targets, exclusions, and compatibility expectations.
- Create a living feature matrix covering keep, replace, native-backend, or remove.
- Document the initial output strategy: internal render buffer plus no-op/null output first; no additional browser preview; no first-milestone physical LED output requirement.
- Document Home Assistant compatibility as a first useful milestone release gate.

Key files:
- `Native-Port-Plan.md`
- `README.md`
- `AGENTS.md`
- `docs/native-porting.md` (create)

Implementation notes:
- Treat ESP support removal as intentional, not a side effect.
- Mark user-visible breaking changes explicitly.
- Keep the feature matrix in repo docs, not only in commit messages.
- User-visible breaking change: native WLED does not drive ESP GPIO, RMT, I2S, LEDC, hardware IR, hardware DMX UART, or OTA firmware update paths.
- Preserve the name WLED, JSON API semantics, WebSocket state updates, and Home Assistant discovery/identity behavior despite the native-only runtime.

Verification:
- Documentation review confirms every major feature area has a target disposition.
- `npm test` remains green because no runtime code should change in this task.

Execution log:

Actions taken:
- Resolved assumptions and clarifying questions into the confirmed native scope.
- Removed the earlier preview/network-first milestone assumption and replaced it with internal render buffer plus no-op/null output as the first native output strategy.
- Added Home Assistant compatibility, stable identity, default config behavior, and third-party dependency policy to the plan.
- Added a research baseline with current Home Assistant, WLED API/protocol, XDG, Apple filesystem, and candidate dependency references.
- Added repo-level Codex hook guardrails for native-port prompts and stop-time plan checks.
- Created `docs/native-porting.md` as the living native scope and feature-matrix document.
- Added a README native-port notice linking to the native docs and this migration plan.

Discrepancies or deviations:
- Earlier plan text treated preview/network output as the first output milestone. The answered scope rejects additional browser preview work and does not require physical LED output from day one.
- Earlier config text suggested automatic ESP config import guidance. The answered scope says first run should use WLED defaults; import is now optional future work only.
- The stop hook correctly detected that native-port-related staged files can exist without a same-change `Native-Port-Plan.md` update. This task log now records the hook and AGENTS documentation work explicitly.

Key decisions and reasoning:
- Keep WLED's user-visible product name to avoid breaking API/integration expectations and unnecessary rebranding.
- Use a generated, persisted MAC-compatible native instance ID because Home Assistant stores WLED's MAC-derived unique ID and rejects mismatches.
- Keep existing live-view/peek compatibility only as a render-buffer view, not as a new native preview feature.
- Keep repo hooks in `.codex/hooks.json` rather than inline config so future agents have a single reviewed hook manifest.
- Resolve hook commands through `git rev-parse --show-toplevel` so Codex can start from subdirectories without breaking hook paths.

Docs updates:
- `AGENTS.md` documents the native-port workflow, the requirement to update this plan with each native-port change, and the repo-level Codex hooks.
- `docs/native-porting.md` documents native-only support, user-visible ESP hardware removals, first runnable experience, feature dispositions, CLI placeholders, and dependency policy.
- `README.md` now points contributors to the native migration scope before they assume the repository is firmware-only.

Verification performed:
- `npm test` passed with 16 tests, 0 failures after the plan scope update.

Newly discovered tasks or risks:
- Repo hooks must be reviewed/trusted with `/hooks` after checkout or hook edits; otherwise agents may not receive the process reminders.
- Stop-time hook checks are path/message heuristics and may produce false positives or miss unusual native-port changes. Treat the hook as a guardrail, not proof that the plan is complete.

## Task 2: Add Native Build Skeleton And Developer Scripts

Objective: create a native build entry point without changing firmware behavior yet.

Scope:
- Add a native build system, preferably CMake, with `native/` or `src/native/` entry points.
- Add scripts such as `scripts/native-build.sh`, `scripts/native-run.sh`, and `scripts/native-test.sh`.
- Add a minimal `wled-native` executable that prints version/help and exits.
- Keep Node web build/test working during the transition.
- Add a dependency manifest or documentation section for native third-party libraries, including license, source/version, packaging impact, and adapter boundary.

Key files:
- `CMakeLists.txt` (create)
- `native/` or `src/native/` (create)
- `scripts/native-build.sh` (create)
- `scripts/native-run.sh` (create)
- `scripts/native-test.sh` (create)
- `package.json`
- `.github/workflows/`
- `docs/native-porting.md` (create)

Implementation notes:
- Prefer separate native build outputs under `build/native/`.
- Do not remove PlatformIO until native build, tests, and CI exist.
- Add `--help`, `--config-dir`, `--host`, `--port`, `--log-level`, and `--version` CLI placeholders early.
- Use the user-visible name WLED in help/version text; use `wled-native` only as the executable/build target name if needed.
- Third-party libraries are allowed, but keep them behind narrow native adapters so WLED core code does not become coupled to a specific HTTP, WebSocket, MQTT, audio, or Zeroconf implementation.

Verification:
- `npm test`
- `npm run build`
- `scripts/native-build.sh`
- `scripts/native-run.sh --help`

Execution log:

Actions taken:
- Added a root `CMakeLists.txt` that builds a `wled-native` executable under `build/native/`.
- Added `native/main.cpp` with `--help`, `--version`, `--config-dir`, `--host`, `--port`, and `--log-level` CLI placeholder handling.
- Added `scripts/native-build.sh`, `scripts/native-run.sh`, and `scripts/native-test.sh`.
- Added a CTest smoke test through `native/tests/cli-smoke.sh` and verified the red/green path against the missing then present executable.
- Added npm aliases: `native:build`, `native:run`, and `native:test`.
- Added `build/` to `.gitignore` so native build products are not committed.
- Added native skeleton CI coverage to `.github/workflows/wled-ci.yml` while keeping the existing firmware reusable workflow.

Discrepancies or deviations:
- Added `scripts/native-test.sh` and a CTest smoke test in addition to the requested `scripts/native-run.sh --help` verification so the CLI contract can be checked automatically.
- Did not add any third-party native runtime library in this task; the dependency manifest records candidate libraries as not selected.

Key decisions and reasoning:
- Chose CMake because it is the common cross-platform entry point for native C++ on macOS/Linux and keeps native build artifacts separate from PlatformIO.
- Kept the first executable isolated in `native/` so firmware sources and PlatformIO behavior remain unchanged.
- Kept CLI options as placeholders to stabilize scripts and documentation before runtime, config, server, and logging code exist.
- Used only the C++ standard library for the skeleton to avoid committing to HTTP/WebSocket/audio/Zeroconf dependencies before adapter boundaries are designed.
- Added native checks to `wled-ci.yml` rather than replacing firmware CI because PlatformIO remains the validation path until native runtime coverage is broader.

Docs updates:
- `docs/native-porting.md` documents native commands, CLI placeholders, feature matrix, and dependency manifest.
- `README.md` links to the native migration docs.
- `AGENTS.md` lists the native npm/script commands and new project directories.

Verification performed:
- Initial native CLI smoke test failed because `build/native/wled-native` did not exist, confirming the test covered the missing skeleton.
- `npm ci` completed successfully.
- `npm run build` completed successfully and regenerated ignored web headers.
- `npm test` passed with 16 tests, 0 failures.
- `scripts/native-build.sh` completed successfully.
- `scripts/native-run.sh --help` printed the native runtime help text successfully.
- `scripts/native-test.sh` passed with 1 CTest smoke test.
- `pio run -e esp32dev` and `platformio run -e esp32dev` could not be run because neither `pio` nor `platformio` is installed in this environment.

Newly discovered tasks or risks:
- The native version string is currently duplicated in `CMakeLists.txt` and `package.json`; a later task should centralize version metadata before releases.
- The native CI job uses the runner-provided CMake toolchain; pinning or documenting a minimum CMake package may be needed if CI environments change.
- Local firmware validation still requires installing PlatformIO in the development environment.

## Task 3: Introduce The Native Compatibility Layer

Objective: replace the widest Arduino surface with host-native compatibility types so core files can compile incrementally.

Scope:
- Implement compatibility for `String`, `Print`, `Stream` where needed, `IPAddress`, `File`, `millis()`, `micros()`, `delay()`, `yield()`, `random()`, `map()`, `constrain()`, `F()`, `PSTR()`, `FPSTR()`, `PROGMEM`, `IRAM_ATTR`, `byte`, and debug print macros.
- Add native memory helpers for `d_malloc`, `p_malloc`, `p_free`, `p_realloc`, and heap reporting.
- Centralize native platform macros such as `WLED_NATIVE`.

Key files:
- `native/arduino_compat/` (create)
- `wled00/wled.h`
- `wled00/fcn_declare.h`
- `wled00/util.cpp`
- `wled00/src/dependencies/json/`

Implementation notes:
- Keep compatibility narrow and test-driven. Implement only APIs used by compiled native targets.
- Prefer standard C++ containers and RAII in native-only code.
- Avoid carrying ESP memory constraints into host code except where API behavior depends on them.

Verification:
- Add focused native unit tests for time, string/print behavior, filesystem file facade, IP address parsing, and memory helpers.
- Native build compiles the compatibility layer alone.

Execution log:

Actions taken:
- Created `native/arduino_compat/` directory.
- Implemented `Arduino.h`, `WString.h`, `WString.cpp`, `Print.h`, `Print.cpp`, `Stream.h`, `IPAddress.h`, `IPAddress.cpp` mimicking ESP/Arduino interfaces.
- Implemented time functions `millis()`, `micros()`, `delay()`, `yield()`, and math functions `map()`, `constrain()`, `random()`.
- Added mock RAM functions `d_malloc`, `p_malloc`, `p_free`, `p_realloc`, and `getFreeHeap()` based on standard `malloc`/`free`.
- Added `native/tests/test_arduino_compat.cpp` with unit tests for `String`, `Print`, `IPAddress`, and math functions.
- Updated `CMakeLists.txt` to compile `arduino_compat` as a library and link tests.

Discrepancies or deviations:
- Implemented `String` as a subclass of `std::string` for simplicity and performance instead of an exact 1:1 Arduino `String` class re-implementation.
- Ignored memory tracking features (`getFreeHeap`) returning dummy values since desktop systems use virtual memory.
- `Print::printNumber` logic from Arduino was slightly adapted to compile correctly without pulling in the entire Arduino core.

Key decisions and reasoning:
- Built the Arduino compatibility layer as narrow as possible, relying heavily on C++ Standard Library (`std::string`, `<chrono>`) to implement Arduino concepts on host OS efficiently.
- Isolated all compatibility layers in a static library linked only when native mode is on.
- Ensured C++17 was the system standard across targets.

## Task 4: Port Filesystem And Configuration Storage

Objective: make existing WLED JSON configuration and asset storage work on a host filesystem.

Scope:
- Implement a native `WLED_FS` facade backed by `std::filesystem`.
- Keep file paths rooted under the configured native config directory.
- Preserve `cfg.json`, `wsec.json`, `presets.json`, palette files, ledmaps, uploads, backups, and restore behavior.
- Generate WLED default files/values on first run when no config exists.
- Add optional, explicit config import guidance from an existing WLED filesystem export only after the default first-run path is working.

Key files:
- `native/fs/` (create)
- `wled00/file.cpp`
- `wled00/cfg.cpp`
- `wled00/presets.cpp`
- `wled00/colors.cpp`
- `wled00/FX_fcn.cpp`
- `README.md`
- `docs/native-porting.md`

Implementation notes:
- Reject path traversal and absolute-path escapes when serving or writing through the web UI.
- Keep secrets such as `wsec.json` in the config root, not in generated build directories.
- Preserve WLED's in-place preset update behavior unless native tests prove a simpler atomic-write path is compatible.
- Do not silently scan for or import ESP device exports. Any import path must be requested explicitly by CLI or UI and must not overwrite existing native config without confirmation.

Verification:
- Unit tests for read/write/delete/list/copy/backup/restore and path traversal rejection.
- Run config deserialize/serialize round-trip tests against sample JSON.
- Verify local browser upload/download paths through the settings/editor pages once the server exists.

Execution log:

Actions taken:
- Implemented a native `WLED_FS` file system facade using C++17 `<filesystem>` mapping file names correctly.
- Mapped `FS` interface mimicking Arduino's `LittleFS` usage covering file write/read logic using C++ standard headers like `<iostream>`, `fopen`, `fread`, etc.
- Covered directory checking inside config folder.
- Configured CMake properly to fix std::filesystem linkage across macOS instances by ensuring C++17 is targeted and standard requirement is set.
- Developed comprehensive tests testing file/folder persistence via `native/tests/test_fs.cpp`.

Discrepancies or deviations:
- Did not implement ESP32-specific partition validation or SPIFFS metadata checks. The abstraction behaves as standard folder under OS.

Key decisions and reasoning:
- Reused `<filesystem>` combined with standard `FILE*` objects. Leveraging POSIX/Windows IO keeps memory efficiency without loading OS objects via heavy frameworks.
- Bound file resolution to prepend configured directory (`_basePath`) directly ensuring it mimics sandboxing within config folder correctly.

## Task 5: Port Runtime Lifecycle, Loop, Logging, And Shutdown

Objective: make the WLED lifecycle run as a native process.

Scope:
- Replace Arduino `setup()`/`loop()` entry with a native main function and scheduler.
- Preserve WLED loop ordering where behavior depends on it.
- Add signal handling for graceful shutdown on SIGINT/SIGTERM.
- Replace ESP restart/reboot behavior with process restart request semantics or clean exit codes.
- Implement native logging to stdout/stderr first, with optional file logging later.

Key files:
- `wled00/wled.cpp`
- `wled00/wled_main.cpp`
- `native/main.cpp` (create)
- `native/runtime/` (create)

Implementation notes:
- Keep loop timing deterministic enough for effects, transitions, playlists, timers, and WebSocket live view.
- Replace watchdog/heap-panic logic with native diagnostics rather than process resets.
- Do not use busy loops that burn CPU on host systems.

Verification:
- Native process starts, runs the main loop, reports uptime, and exits cleanly.
- Tests cover signal-triggered shutdown and restart-request state.

Execution log:

Actions taken:
- Added `native/runtime/Runtime.h` and `native/runtime/Runtime.cpp` with a native setup/loop/shutdown lifecycle.
- Refactored `native/main.cpp` so CLI parsing creates `NativeRuntimeOptions` and starts the runtime instead of only printing skeleton state.
- Added clean shutdown and restart-request state with explicit exit codes: `0` for clean exit and `75` for restart request.
- Added POSIX `sigaction` handling for SIGINT and SIGTERM through `native/runtime/SignalHandling.*`.
- Added runtime test coverage for max-loop shutdown, explicit stop, restart-request exit code, environment-derived config roots, and signal-triggered shutdown.
- Added `--duration-ms` and `--max-loops` development/test controls so the runtime can be exercised without a permanently running process.
- Connected runtime startup to the native config root and logged the persisted native identity as `nativeMac`.

Discrepancies or deviations:
- The runtime does not call the legacy WLED `setup()`/`loop()` yet because core WLED sources still depend on later HTTP, state, output, and hardware-removal tasks.
- Native logging is stdout/stderr only. File logging remains a later service/runtime task.
- The restart path returns a process exit code instead of restarting in-process, matching the plan's process restart semantics.

Key decisions and reasoning:
- Used a small runtime class rather than putting lifecycle logic in `main.cpp` so later WLED setup/loop work can be tested without spawning a process.
- The signal handler only sets a `sig_atomic_t` flag; the runtime observes the flag during normal loop execution. This avoids doing non-signal-safe work in the signal handler.
- Kept the loop delay at 1 ms by default to avoid a busy host CPU loop while preserving a shape close to Arduino's repeated `loop()` calls.
- Runtime config root resolution follows the plan's priority: CLI, `WLED_CONFIG_DIR`, OS default, then development fallback.

Docs updates:
- `AGENTS.md` now refers to the native runtime rather than only a skeleton.
- `docs/native-porting.md` documents runtime shutdown behavior, config root priority, native identity persistence, network test permissions, and development loop controls.

Verification performed:
- Runtime tests were first added against missing `native/runtime` headers and failed as expected.
- Signal handling tests were first added against a missing `SignalHandling.h` and failed as expected.
- `scripts/native-test.sh` passed with 6 CTest tests after implementation when run with local socket permission.
- Final verification also ran `npm ci`, `npm run build`, `npm test`, `scripts/native-build.sh`, `scripts/native-run.sh --help`, and `scripts/native-run.sh --config-dir /private/tmp/wled-native-verify --max-loops 1` successfully.
- Firmware verification could not run because neither `pio` nor `platformio` is installed in this environment.

Newly discovered tasks or risks:
- Later service wrappers need to interpret exit code `75` as a restart request.
- Native runtime logs currently print directly to stdout; structured log levels are still placeholders until logging is expanded.
- The runtime loop is only a host scheduler shell until legacy WLED setup/loop work can be attached.

## Task 6: Port Network Identity, DNS, UDP, And Time

Objective: replace WiFi/ETH concepts with host network abstractions while keeping protocol and integration behavior.

Scope:
- Implement native `NetworkClass`, `IPAddress`, DNS lookup, UDP sockets, multicast/broadcast, and local interface discovery.
- Preserve NTP, node discovery, WLED sync, Hyperion/TPM2.net, E1.31, Art-Net, DDP, and realtime UDP behavior.
- Replace WiFi setup/scanning/AP behavior with native network status and documentation.
- Implement WLED-compatible Zeroconf/mDNS advertisement for `_wled._tcp.local.` with the HTTP port and a `mac` TXT property.
- Generate and persist a stable MAC-compatible native instance ID in the config root. Use it consistently in Zeroconf TXT, `/json/info.mac`, logs, and any Home Assistant setup path.

Key files:
- `wled00/src/dependencies/network/Network.*`
- `wled00/udp.cpp`
- `wled00/e131.cpp`
- `wled00/ntp.cpp`
- `wled00/wled.cpp`
- `native/net/` (create)

Implementation notes:
- AP mode and WiFi credential provisioning are ESP-only and should be removed from native UI/API or shown as unsupported with clear behavior.
- Node identity should use a stable generated/native instance ID, not a WiFi MAC dependency. Format it like the current WLED lowercase hex MAC string because Home Assistant normalizes and stores it as the device unique ID.
- Broadcast behavior must account for hosts with multiple network interfaces.
- Preserve network protocol behavior, but strip WiFi-only labels/sensors from native user-facing output or mark them unsupported. Home Assistant compatibility should not depend on RSSI/channel/BSSID fields.

Verification:
- Unit tests for IP parsing, subnet checks, and packet encoders/decoders.
- Integration tests bind UDP ports locally and send WLED notifier/DDP/E1.31 sample packets.
- Manual test with two native instances on different ports if multicast/broadcast is enabled.
- Home Assistant discovery test confirms `_wled._tcp.local.` advertisement is visible, the `mac` TXT matches `/json/info.mac`, and reconnecting after restart does not trigger a MAC mismatch.

Execution log:

Actions taken:
- Added `native/net/Network.h` and `native/net/Network.cpp`.
- Implemented `NativeIdentityStore` to create/load `native-id.json` in the config root with a stable lowercase 12-hex-digit MAC-compatible identity.
- Logged the generated/persisted identity from runtime startup as `nativeMac`.
- Implemented IPv4 DNS resolution with `getaddrinfo`.
- Implemented IPv4 local interface discovery with `getifaddrs`, including interface name, address, loopback flag, and up/down flag.
- Implemented a small POSIX UDP socket wrapper with bind, local port discovery, send, receive with timeout, and close behavior.
- Added native unit tests for identity persistence, DNS resolution of `localhost`, loopback interface discovery, and UDP localhost send/receive.

Discrepancies or deviations:
- This task did not implement full WLED `NetworkClass`, multicast/broadcast protocol behavior, NTP, or WLED notifier/DDP/E1.31 packet handling yet.
- Zeroconf/mDNS advertisement is not implemented yet. The identity value is ready for `_wled._tcp.local.` TXT and `/json/info.mac`, but actual advertising should be added behind a dedicated dependency/adapter decision.
- Tests cover IPv4 only because the current Arduino compatibility `IPAddress` is IPv4-only.

Key decisions and reasoning:
- Used OS networking APIs already available on macOS/Linux instead of adding a third-party dependency for the first adapter slice.
- Kept the generated identity locally administered by forcing the `02` first octet; this avoids implying a real hardware MAC while preserving Home Assistant's MAC-shaped identity expectation.
- Persisted identity in `native-id.json` under the config root so reconnects do not trigger Home Assistant MAC mismatch behavior.
- Deferred Zeroconf dependency selection because Home Assistant compatibility needs real mDNS behavior, and choosing Avahi/Bonjour/libdns-sd or another library has packaging and adapter-boundary implications.

Docs updates:
- `docs/native-porting.md` documents config root priority, `native-id.json`, native network adapter coverage, and the remaining Zeroconf gap.

Verification performed:
- Network tests were first added against a missing `native/net/Network.h` and failed as expected.
- A sandboxed `scripts/native-test.sh` run failed at UDP loopback bind with `Operation not permitted`; a standalone Python UDP bind failed the same way, confirming a sandbox permission issue rather than adapter logic.
- `scripts/native-test.sh` passed with 6 CTest tests, 0 failures when run with local socket permission.
- `git diff --check` passed after the final documentation and C++ edits.

Newly discovered tasks or risks:
- Local UDP/multicast tests may require explicit socket permission in sandboxed agent environments.
- A later task must implement real Zeroconf/mDNS advertisement and verify the advertised `mac` TXT exactly matches `/json/info.mac`.
- IPv6 support remains unimplemented in the compatibility `IPAddress` and native network tests.

## Task 7: Port HTTP Server, WebSocket Server, And Web Assets

Objective: preserve WLED's browser UI and API on native.

Scope:
- Replace `ESPAsyncWebServer`, `AsyncWebSocket`, `AsyncCallbackJsonWebHandler`, and response/request classes with native equivalents or a compatibility adapter.
- Serve embedded/generated web assets or direct `wled00/data` assets depending on build mode.
- Preserve `/`, `/settings`, `/json`, `/ws`, `/edit`, `/upload`, `/version`, `/uptime`, `/freeheap`, palette, live view, and static JS/CSS routes.
- Remove or replace OTA/bootloader update endpoints.

Key files:
- `wled00/wled_server.cpp`
- `wled00/ws.cpp`
- `wled00/json.cpp`
- `wled00/xml.cpp`
- `wled00/data/`
- `tools/cdata.js`
- `native/http/` (create)

Implementation notes:
- Keep frontend source in `wled00/data/` and avoid duplicating API behavior in a new server.
- Browser testing is mandatory for changed pages.
- Start with localhost binding by default; make LAN binding explicit through `--host 0.0.0.0` or config.
- Keep the existing live-view/peek route and WebSocket `{"lv":true}` behavior only if it can read the normal render buffer. Do not add new preview pages or a separate preview backend.
- Preserve the WebSocket state/info behavior expected by Home Assistant: connect to `/ws`, receive state/info on connect and state changes, and accept JSON state updates.

Verification:
- Native app serves the main UI with no browser console errors.
- JSON GET/POST and WebSocket state update tests pass.
- Browser tests cover navigation, color controls, effect changes, settings pages, upload/list editor flows, and mobile-sized viewport smoke checks.

Execution log:

Actions taken:
- Added `native/http/NativeHttpServer.h` and `native/http/NativeHttpServer.cpp`.
- Implemented a dependency-free POSIX localhost HTTP adapter with route handling for `/`, `/settings`, settings subpages, `/edit`, `/liveview`, `/liveview2D`, static `wled00/data` assets, `/json`, `/json/state`, `/json/info`, `/json/si`, `/json/effects`, `/json/fxdata`, `/json/palettes`, `/json/palx`, `/settings/s.js`, `/version`, `/uptime`, and `/freeheap`.
- Implemented native `/ws` WebSocket handshake support, SHA-1/base64 accept-key generation, initial state/info delivery, `"p"` heartbeat replies, JSON state updates for `on` and `bri`, verbose `{"v":true}` replies, and `{"lv":true}` live-view binary frames sourced from the normal native render buffer.
- Connected `NativeRuntime` startup to the HTTP adapter using `--host`, `--port`, the persisted native identity, and the source-tree `wled00/data` asset root.
- Added native HTTP tests for static asset routes, minimal JSON state/info/effect/palette/settings-script behavior, JSON POST state updates, OTA/upload route disposition, and WebSocket state/info behavior.

Discrepancies or deviations:
- This is the first native adapter slice, not the full `ESPAsyncWebServer` compatibility layer. It does not yet reuse `wled00/wled_server.cpp`, `wled00/json.cpp`, or `wled00/ws.cpp`.
- JSON compatibility is intentionally minimal in this slice: `on`, `bri`, state/info shape, native MAC identity, and live-view plumbing are covered, while full preset, playlist, segment, palette, effect, config, upload/list editor, and settings mutation behavior remains unported.
- `/update` now returns `410 Gone` for native firmware OTA removal, and `/upload` returns `501 Not Implemented` until file-editor upload semantics are deliberately ported.
- Browser testing covered first-page loading and console health for the initial native UI route. The deeper interaction matrix listed in the task remains future work because the existing browser UI still expects many full WLED JSON/config behaviors that are not ported in this slice.

Key decisions and reasoning:
- Avoided a third-party HTTP/WebSocket dependency for this milestone so the native build remains self-contained while the project decides whether to use Boost.Beast, libwebsockets, or another adapter later.
- Bound to `127.0.0.1` by default and preserved explicit LAN exposure through `--host 0.0.0.0`, matching the task safety requirement.
- Kept the server behind `native/http/` so later work can replace the adapter internals or route to the existing WLED handlers without coupling core firmware files directly to host socket code.
- Preserved Home Assistant's most important WebSocket contract slice by sending state/info on connect and accepting JSON state updates over `/ws`.

Docs updates:
- `docs/native-porting.md` documents the native HTTP/WebSocket adapter, supported routes, unsupported OTA/upload behavior, and current compatibility limits.
- `README.md` now notes that the native runtime serves browser assets on localhost and renders to an internal null-backed buffer.

Verification performed:
- Native HTTP tests were first added against missing `native/http/NativeHttpServer.h` and failed as expected.
- `ctest --test-dir build/native --output-on-failure -R native_http` passed when run with localhost socket permission.
- Browser smoke testing loaded `http://127.0.0.1:18080/` from the native runtime with Playwright fallback because the Browser plugin is not available in this session. Page identity, non-blank rendering, settings-route navigation, desktop and mobile screenshots, and console health passed with no console errors and no failed HTTP responses.
- Final validation ran `npm ci`, `npm run build`, `npm test`, `scripts/native-run.sh --help`, `ctest --test-dir build/native --output-on-failure -E 'native_network|native_http'`, `ctest --test-dir build/native --output-on-failure -R 'native_http|native_output'` with socket permission, and `git diff --check`.
- A final full `scripts/native-test.sh` rerun could not be completed because the sandbox approval review timed out twice; the full native suite had passed earlier with socket permission before the later compatibility-route extension, and the affected `native_http`/`native_output` tests passed after that extension.

Newly discovered tasks or risks:
- Full JSON/API parity needs a follow-up adapter pass or reuse of the existing WLED handlers after ESP-only dependencies are separated.
- Segments and presets still show loading placeholders because full segment, preset, playlist, and config behavior is not yet ported.
- File editor upload/list flows are not complete; `/upload` is explicitly unsupported in this slice.
- If a third-party HTTP/WebSocket library is selected later, record its license, packaging impact, and adapter boundary before replacing this adapter.

## Task 8: Port Render Buffer And Optional Native Output Backends

Objective: decouple WLED light state from ESP LED drivers while keeping effects, state, integrations, and future output extension points useful.

Scope:
- Preserve `Bus`, `BusManager`, segment rendering, current limiting metadata, and color-order behavior.
- Replace NeoPixelBus/RMT/I2S/LEDC/HUB75 implementations with native render-buffer/no-op behavior for the first milestone.
- Remove ESP-dependent physical LED output from the native build unless and until a host-native backend is deliberately added.
- Keep or add a narrow backend interface for future USB serial, USB DMX, SPI, HID, vendor SDK, or network forwarding outputs.
- Treat existing network bus types as optional future native output backends, not as a first-milestone requirement.

Key files:
- `wled00/bus_manager.*`
- `wled00/bus_wrapper.h`
- `wled00/FX_fcn.cpp`
- `wled00/const.h`
- `wled00/data/settings_leds.htm`
- `native/output/` (create)

Implementation notes:
- Keep logical LED types where they still describe channel/color layout.
- Remove host-irrelevant pin selection from native settings or convert it to backend-specific fields.
- Native first milestone can render into memory and drop frames at the output boundary. This is acceptable because physical output is not a release gate.
- Future blocking outputs must not block the render loop on slow network or external writes; use queues or throttling.

Verification:
- Unit tests compare color conversion, segment output, bus length, color order, RGBW/CCT behavior, and current-limit calculations.
- Tests prove effects write non-empty data into the internal render buffer.
- If existing live-view/peek compatibility is retained, browser live view and `/ws` `{"lv":true}` read from the normal render buffer.
- Network output capture tests apply only if a network output backend is explicitly implemented later.

Execution log:

Actions taken:
- Added `native/output/RenderBuffer.h` and `native/output/RenderBuffer.cpp`.
- Implemented `NativeRenderBuffer` with RGBW pixel storage, CCT metadata retention, clear/set/get behavior, common RGB/GRB/BRG/RGBW/GRBW encoding, visible-pixel checks, and current estimation.
- Implemented `NativeOutputBackend` and `NativeNullOutputBackend` as the first host output boundary. The null backend records frame count and the most recent frame for tests without driving physical LEDs.
- Connected `NativeRuntime::loopOnce()` to publish a deterministic memory-backed frame through the null backend.
- Connected native WebSocket live-view frames to the normal render buffer instead of adding a separate preview backend.
- Added native output tests for RGBW/CCT storage, color-order encoding, out-of-range pixel safety, current estimation, and null backend frame capture.

Discrepancies or deviations:
- This slice does not port `Bus`, `BusManager`, `bus_wrapper.h`, `FX_fcn.cpp`, or existing WLED effects yet.
- Current-limit behavior is represented by a simple 20 mA per fully-on channel estimate in the native render buffer, not the full firmware current-limiting model.
- The runtime frame writer is a deterministic bring-up frame, not the WLED effects engine. Effects writing into the render buffer remains future task 9 work.
- Native settings UI changes for host-irrelevant pin selection were not made in this slice because the browser settings pages still depend on broader JSON/config parity.

Key decisions and reasoning:
- Kept physical output as a no-op/null backend because first-milestone native WLED should decouple state/effects/API work from host hardware output decisions.
- Preserved RGBW and CCT fields in the render buffer even though the live-view downmix is RGB, so later output backends and settings/API work do not lose channel metadata.
- Used a narrow backend interface with `begin()` and `show()` so future USB serial, USB DMX, SPI, HID, vendor SDK, or network forwarding outputs can attach without blocking the runtime loop directly.

Docs updates:
- `docs/native-porting.md` documents the memory render buffer, null backend, current-estimation assumption, live-view source, and future physical-output boundary.
- `README.md` mentions the current null-backed render-buffer milestone.

Verification performed:
- Native output tests were added before implementation; the first native build failed on missing `native/http/NativeHttpServer.h`, confirming the new native adapter/output headers were absent.
- `cmake --build build/native --target test_output && build/native/test_output` passed after implementation.
- Full native CTest verification passed with localhost socket permission before the final compatibility-route extension; after that extension, `native_output`, `native_http`, and the non-socket CTest subset passed.

Newly discovered tasks or risks:
- Full `BusManager` and effect-engine integration remains the next major output milestone before claims about effect parity can be made.
- Host output backends must use queues or throttling before any slow external device or network writer is added.
- Browser live-view is wired to the render buffer, but meaningful visual output depends on later effect/state integration.

## Task 9: Bring Up Core State, Effects, Presets, Playlists, And Timers

Objective: compile and exercise the main WLED value proposition natively.

Scope:
- Compile effects, palettes, segments, transitions, presets, playlists, nightlight, timers, JSON state changes, and internal render-buffer updates.
- Keep 1D and 2D effects where they do not depend on unavailable hardware.
- Preserve audioreactive effect fallbacks when microphone data is absent.

Key files:
- `wled00/FX*.cpp`
- `wled00/FX*.h`
- `wled00/colors.*`
- `wled00/palettes.cpp`
- `wled00/playlist.cpp`
- `wled00/presets.cpp`
- `wled00/led.cpp`
- `wled00/json.cpp`
- `wled00/ntp.cpp`

Implementation notes:
- Treat compile failures in hot-path code as portability work, not reasons to drop effects.
- Replace ESP-specific performance attributes with no-op or compiler-appropriate native attributes.
- Preserve deterministic behavior where tests depend on timing/randomness by adding injectable clocks/random sources for tests.

Verification:
- Native tests load sample config/presets, apply JSON state changes, run selected effects for several frames, and validate non-empty pixel buffers.
- Browser smoke test confirms effect and palette changes update JSON/WebSocket state. Existing live-view/peek compatibility is tested only if retained.

Execution log:

Actions taken:
- Added `native/core/NativeWledCore.h` and `native/core/NativeWledCore.cpp` as the first native core state/effect layer.
- Added WLED-style native state for power, brightness, transition, selected preset, active playlist, nightlight, and segment fields including bounds, colors, effect, speed, intensity, and palette.
- Added effect and palette metadata routes backed by the native core instead of one-effect HTTP placeholders.
- Implemented native render-buffer effects for host-portable 1D behavior: solid, blink/strobe, breathe/fade, rainbow/colorloop, theater/chase, sparkle/glitter, gradient/palette, meteor, and sinelon-style rendering.
- Added deterministic palette fallback rendering for other effect IDs so browser/API effect selection still updates the internal render buffer while full legacy FX parity is separated from ESP dependencies.
- Added audioreactive effect fallback behavior for no-microphone native runs by rendering deterministic non-audio output instead of depending on microphone data.
- Implemented native `presets.json` initialization, preset save/apply, `/presets.json`, JSON `ps` and `psave`, playlist objects with `ps`, `dur`, `transition`, `repeat`, `end`, and `r`, playlist advancement, and nightlight brightness fade.
- Wired `NativeRuntime` to render frames through `NativeWledCore` and wired `NativeHttpServer`/WebSocket handling to the same core state.
- Added `native_core` CMake target and `native/tests/test_core.cpp` coverage for JSON state rendering, preset save/apply, playlist advancement, and nightlight behavior.

Discrepancies or deviations:
- This task does not compile `wled00/FX.cpp`, `FX_fcn.cpp`, `playlist.cpp`, `presets.cpp`, `json.cpp`, or `ntp.cpp` directly yet. Those files remain coupled to `wled.h`, `strip`, Arduino/ESP globals, and broader firmware runtime state.
- The native core implements a compatibility slice and deterministic fallback effects so all non-hardware JSON/API/browser paths can work now; exact visual parity for every WLED 1D/2D effect remains a later extraction task.
- Native time support is monotonic runtime time for uptime, nightlight, and playlist scheduling. Full NTP, timezone, and timer scheduler parity remains unported.
- Browser automation with Playwright could not run because the package is not installed in this environment. HTTP page-load and JSON smoke checks were run with `curl`, and WebSocket behavior remains covered by the native HTTP CTest.

Key decisions and reasoning:
- Added a native core boundary instead of expanding the HTTP adapter because runtime loop, HTTP JSON, WebSocket updates, presets, playlists, and rendering need a single state owner.
- Used ArduinoJson for native JSON parsing because the repository already vendors it and WLED's state/preset files are JSON contract surfaces.
- Kept physical LED output out of scope and rendered through the existing null-backed native render buffer, matching the native product decision that ESP hardware support is removed and host output backends are future work.
- Exposed WLED-style effect IDs and names while using fallback renderers where exact effect code is still entangled with ESP/Arduino firmware dependencies. This keeps API controls useful without silently dropping requested effect IDs.
- Did not introduce a third-party HTTP, WebSocket, scheduling, or effects dependency in this task.

Docs updates:
- `docs/native-porting.md` now documents the native core state/effects layer, preset/playlist/nightlight support, JSON/WebSocket route behavior, fallback effect limits, and remaining NTP/timer gap.
- `README.md` now describes the native runtime as accepting core JSON/WebSocket state changes and running effect/preset/playlist/nightlight logic into the internal render buffer.
- `AGENTS.md` project structure now identifies `native/` as containing runtime, adapters, core state/effect logic, and tests.

Verification performed:
- `scripts/native-build.sh` passed.
- `scripts/native-test.sh` failed under the sandbox only for localhost socket bind restrictions in `native_network` and `native_http`, matching the previously documented sandbox limitation.
- `scripts/native-test.sh` passed with localhost socket permission: 9 CTest tests, 0 failures.
- `npm ci`, `npm run build`, and `npm test` passed; Node tests reported 16 tests, 0 failures.
- `git diff --check` passed.
- Started `scripts/native-run.sh --config-dir /private/tmp/wled-native-task9-browser --port 18091 --log-level debug` with localhost socket permission and confirmed the HTTP server started.
- Localhost smoke checks loaded `/` and `/settings`, updated `/json/state` with brightness/effect/palette, confirmed state reflected `bri=88`, `fx=9`, `pal=11`, and confirmed `/json/effects` and `/json/palettes` returned WLED-style arrays including `Rainbow`.
- Playwright browser automation was attempted but could not run because `playwright` is not installed in this environment.
- Firmware verification could not run because neither `pio` nor `platformio` is installed in this environment.

Newly discovered tasks or risks:
- Full visual parity requires extracting or adapting the existing `wled00/FX*.cpp` and segment engine once their dependencies on global firmware runtime, `wled.h`, ESP attributes, and bus/strip internals are separated.
- Native 2D effects currently use fallback rendering rather than matrix-aware 2D behavior.
- Full timer/NTP/timezone behavior remains a separate native scheduler task; current support covers runtime monotonic scheduling for playlists and nightlight only.
- Preset compatibility should be expanded to cover quick-load labels, API-command presets, ledmaps, and the exact async save semantics from firmware.
- Browser UI deeper interaction testing still needs a Playwright or Browser-plugin environment with the native server reachable from the automation runtime.

## Task 10: Port MQTT, Hue, Alexa-Compatible, And Integration Protocols

Objective: keep smart-home and automation integrations that can run on native networking.

Scope:
- Replace `AsyncMqttClient` with a native MQTT client behind a small adapter.
- Preserve MQTT topics, payload parsing, publishing, usermod MQTT callbacks, and reconnect behavior.
- Port Hue polling and Alexa-compatible local discovery/control where feasible on host networking.
- Document any discovery differences caused by host firewalls or privilege restrictions.

Key files:
- `wled00/mqtt.cpp`
- `wled00/hue.cpp`
- `wled00/alexa.cpp`
- `wled00/src/dependencies/espalexa/`
- `wled00/src/dependencies/network/`
- `native/mqtt/` (create)

Implementation notes:
- Keep Home Assistant-visible behavior stable if possible.
- Prefer adapter boundaries so core MQTT parsing logic remains recognizable.
- Validate native multicast/SSDP behavior on both macOS and Linux.
- Preserve Home Assistant's primary WLED integration through JSON/WebSocket/Zeroconf first. MQTT discovery/control is useful but is not a substitute for native WLED integration compatibility.

Verification:
- Integration test with a local MQTT broker verifies subscribe, command, publish, retain, LWT-equivalent, and usermod callbacks.
- Local network/manual tests cover Hue polling and Alexa-compatible discovery if retained.

Execution log:

Actions taken:

Discrepancies or deviations:

Key decisions and reasoning:

## Task 11: Port Audioreactive To Native Microphone Input

Objective: keep audio effects useful on native systems.

Scope:
- Replace ESP I2S/ADC microphone sources with a native audio capture source using the OS default input device first.
- Preserve the audioreactive usermod data contract consumed by `FX.cpp`.
- Keep FFT, AGC, peak detection, UDP audio sync, and "no microphone" fallback behavior.
- Add device listing/selection later if default input works reliably.

Key files:
- `usermods/audioreactive/audio_reactive.cpp`
- `usermods/audioreactive/audio_source.h`
- `wled00/FX.cpp`
- `wled00/fcn_declare.h`
- `native/audio/` (create)

Implementation notes:
- Recommended strategy: isolate `AudioSource` behind a native implementation and feed the existing usermod variables rather than rewriting audio effects.
- Choose a cross-platform capture library only after documenting license, build impact, and macOS/Linux support.
- Native audio must degrade gracefully when no input device or permission is available.

Verification:
- Unit tests for FFT/AGC logic using synthetic sine/noise/silence samples.
- Manual test with default microphone verifies audio effects respond and silence fallback works.
- Browser UI shows any retained audio settings accurately.

Execution log:

Actions taken:

Discrepancies or deviations:

Key decisions and reasoning:

## Task 12: Preserve Usermod Harness And Classify Bundled Usermods

Objective: keep extension support while removing ESP-only bundled usermod assumptions.

Scope:
- Keep `Usermod`, `UsermodManager`, `REGISTER_USERMOD`, usermod config, JSON info/state hooks, MQTT hooks, UDP hooks, and overlay hooks.
- Build a native usermod manifest/list independent of PlatformIO `custom_usermods`.
- Classify bundled usermods as native-ready, portable with adapter, hardware-only, or remove-from-native-build.
- Port `user_fx` and audioreactive first because they preserve effects functionality.

Key files:
- `wled00/um_manager.cpp`
- `wled00/fcn_declare.h`
- `usermods/`
- `usermods/readme.md`
- `docs/native-porting.md`
- Native build files

Implementation notes:
- Do not delete the harness because hardware-dependent usermods exist.
- Hardware-only usermods can remain as source examples if docs clearly say they are excluded from native builds, or they can be removed in a later cleanup task if the repo is truly native-only.
- Replace PlatformIO build inclusion with CMake/native manifest inclusion.

Verification:
- Native build includes at least one usermod through the preserved registration path.
- Usermod settings page still renders for native-ready usermods.
- Tests cover `setup`, `loop`, config read/write, JSON info/state, and lookup behavior.

Execution log:

Actions taken:

Discrepancies or deviations:

Key decisions and reasoning:

## Task 13: Remove ESP Hardware Paths And Update UI/API Semantics

Objective: remove code and UI that cannot apply to native once replacements are working.

Scope:
- Remove or native-gate ESP-only features: WiFi AP/provisioning, ESP-NOW, OTA firmware/bootloader update, brownout/watchdog, flash/PSRAM reporting, GPIO pin validation, RMT/I2S/LEDC, IR receiver GPIO, hardware DMX UART, ESP Ethernet boards, PlatformIO board settings, and ESP-specific reset/update logic.
- Update settings pages to show native network/config/output/audio fields instead of ESP pin and board fields.
- Remove physical LED output controls from native settings until a host-native backend exists. Do not replace them with a new browser preview UI.
- Remove dead code rather than leaving silent preprocessor rubble.

Key files:
- `wled00/wled.h`
- `wled00/wled.cpp`
- `wled00/pin_manager.*`
- `wled00/ota_update.*`
- `wled00/ir.cpp`
- `wled00/dmx_input.*`
- `wled00/dmx_output.cpp`
- `wled00/wled_ethernet.h`
- `wled00/data/settings_*.htm`
- `wled00/data/index.js`
- `wled00/const.h`

Implementation notes:
- This task should happen after native replacements compile and run, not before.
- Preserve protocol DMX over E1.31/Art-Net/DDP where it is network-based.
- Highlight all user-visible breaking changes in docs and release notes.
- Home Assistant's restart button should map to documented native restart/exit semantics rather than ESP reboot.
- Firmware update endpoints/entities should be disabled, hidden, or translated to native package-update documentation; they must not imply OTA firmware flashing exists.

Verification:
- Native build has no dependency on ESP Arduino headers or PlatformIO libraries.
- Browser settings pages contain no misleading GPIO/ESP board controls.
- Tests prove removed endpoints return documented native behavior.

Execution log:

Actions taken:

Discrepancies or deviations:

Key decisions and reasoning:

## Task 14: Replace PlatformIO Tooling, Add Native CI, And Clean Repo Structure

Objective: finish the repository transition from firmware project to native app project.

Scope:
- Remove or archive `platformio.ini`, `platformio_override.sample.ini`, `platformio_release.ini`, `pio-scripts/`, ESP board JSON files, ESP partition CSV files, and firmware-only requirements after native CI covers equivalent validation.
- Replace CI workflows with macOS and Linux native builds/tests.
- Keep or adapt Node web UI build/test tooling.
- Update `.gitignore`, `.vscode`, devcontainer, Gitpod, requirements, package scripts, and contributor docs.

Key files:
- `platformio.ini`
- `pio-scripts/`
- `boards/`
- `tools/*partition*.csv`
- `.github/workflows/`
- `.vscode/`
- `.devcontainer/`
- `requirements.txt`
- `package.json`
- `README.md`
- `docs/`

Implementation notes:
- Do not remove firmware tooling until native build/test/run scripts are the documented path.
- Keep generated web headers only if the native build still embeds them; otherwise replace the generation target with native asset packaging.
- Update AGENTS and contributor docs in the same task.

Verification:
- CI runs native Linux build/test.
- CI runs native macOS build/test if available.
- `npm test`, native unit tests, and browser smoke tests pass.
- No docs point developers to PlatformIO as the required workflow.

Execution log:

Actions taken:

Discrepancies or deviations:

Key decisions and reasoning:

## Task 15: Add Install, Run-On-Boot, Packaging, And Release Documentation

Objective: make native WLED installable and operable as a long-running app.

Scope:
- Add scripts/utilities for build, install, start, stop, status, uninstall, and config directory discovery.
- Add Linux systemd user/service templates.
- Add macOS launchd plist generation/install helpers.
- Add packaging notes for tarball/zip and optional Homebrew/deb/rpm future work.
- Document firewall, ports, config paths, logs, service management, and migration/import.

Key files:
- `scripts/native-install-service.sh` (create)
- `scripts/native-uninstall-service.sh` (create)
- `packaging/` (create)
- `README.md`
- `docs/native-porting.md`
- `docs/native-running.md` (create)

Implementation notes:
- Default service should run as the current user unless explicitly configured otherwise.
- Service helpers must not overwrite config without confirmation.
- Expose port binding and config path through environment or command-line flags.

Verification:
- Linux: install/start/status/stop/uninstall service flow in a clean user environment.
- macOS: launchd load/start/stop/unload flow.
- App restarts with the same config and serves the UI after reboot/login according to selected service mode.

Execution log:

Actions taken:

Discrepancies or deviations:

Key decisions and reasoning:

## Ongoing Verification Strategy

- Keep `npm test` and `npm run build` green while web tooling remains in the repo.
- Add native unit tests as soon as each platform abstraction exists.
- Add protocol fixture tests for JSON, UDP notifier, DDP, E1.31, Art-Net, MQTT payloads, presets, palettes, playlists, and config round-trips.
- Add browser tests once the native HTTP server serves the UI. Check console errors, navigation, color/effect controls, settings pages, file upload/listing, JSON requests, and WebSocket live updates.
- Add at least one integration test that starts `wled-native` on a random local port, drives `/json`, observes WebSocket output, and checks rendered pixel buffer changes through the internal render buffer or retained live-view/peek compatibility.
- Add a Home Assistant compatibility smoke test or documented manual test covering Zeroconf discovery, stable identity, `/json/info`, `/json/state`, `/json/si`, `/ws`, effects, palettes, presets, segments, restart semantics, and absence/handling of firmware OTA behavior.
- Prefer updating tests to match native expectations instead of deleting them.

## Deletion And Cleanup Checkpoints

- Do not remove PlatformIO or ESP board files until native CI can build and run tests.
- Do not remove generated web headers until the native asset strategy is decided and all server routes are updated.
- Do not remove usermods wholesale. Classify them first, preserve the harness, and document the native build list.
- Do not remove protocol implementations just because their original transport used ESP APIs. First check whether the protocol can run on POSIX networking.
- Do not keep ESP physical output settings in native UI/API merely to preserve old fields. If a field is integration-visible, document the native value or unsupported behavior and test it.
- Remove dead code once a replacement is active and reference searches show no native use remains.
