# WLED Native Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the native host binary beyond storage/bootstrap by compiling real WLED core code from `wled00/` without adding a broad Arduino/ESP emulation layer.

**Architecture:** Start with the smallest original core code that can be host-enabled in place without dragging in the full render stack: `prng.h` plus the 32-bit color utility logic currently living in `colors.cpp`. Add only narrow host support for random-number plumbing and host CLI entry points that exercise the compiled WLED core.

**Tech Stack:** C++17, CMake, Node.js `node:test`, existing `scripts/native-*.sh` wrappers

---

### Task 1: Make the smallest WLED core headers compile on host

**Files:**
- Modify: `wled00/prng.h`

- [ ] Remove the hard `wled.h` include from `wled00/prng.h` and replace it with direct standard-library integer includes only.

### Task 2: Compile original WLED color core on host

**Files:**
- Create: `wled00/wled_host_core.h`
- Create: `wled00/wled_host_core.cpp`
- Modify: `CMakeLists.txt`

- [ ] Add a narrow host core support header/source that provides only the missing host-side pieces used by the extracted color/PRNG utilities.
- [ ] Keep the host build limited to the 32-bit color helpers that do not require FastLED/palette/runtime state.
- [ ] Avoid adding `fastled_slim` or `pgmspace` to the host binary until the broader render/palette port actually needs them.

### Task 3: Expose the ported core through the native CLI

**Files:**
- Modify: `wled00/wled_host_cli.h`
- Modify: `wled00/wled_host_cli.cpp`
- Modify: `wled00/wled_host_runtime.cpp`

- [ ] Add native CLI options that exercise the newly host-enabled WLED core without inventing a separate demo app.
- [ ] Support at least color blend/fade and PRNG sequence output so the host binary proves it is running original WLED core code.
- [ ] Keep the CLI output deterministic enough for automated tests where possible.

### Task 4: Extend native CLI coverage for the new core slice

**Files:**
- Modify: `test/native-cli.test.js`
- Modify: `scripts/native-test.sh`

- [ ] Add assertions for the new CLI options and their output format.
- [ ] Keep the new checks inside the existing native smoke flow instead of adding a side test harness.

### Task 5: Update native-port status and execution notes

**Files:**
- Modify: `Native-Port-Plan.md`

- [ ] Record the new host-enabled core slice under the relevant accomplishment area.
- [ ] Document why the current change stops at color/PRNG core instead of forcing all of `cfg.cpp` or `wled_server.cpp` into the host build prematurely.
- [ ] Record that verification was intentionally deferred until the full edited slice was complete, per user instruction.
