# WLED - Native Host Runtime Migration

WLED is being converted from ESP32/ESP8266 firmware into a native macOS/Linux
application built from the original `wled00/` source tree. The project consists
of C++ runtime code and a modern web interface.

Always reference these instructions first and fall back to search or shell
commands only when local guidance does not match the repository state.

## Setup

- Node.js 20+ (see `.nvmrc`)
- Install dependencies: `npm ci`
- Native-port tasks: follow `Native-Port-Plan.md` and the `scripts/native-*.sh`
  wrappers instead of a separate native workflow document

## Build and Test

| Command | Purpose | Typical Time |
|---|---|---|
| `npm run build` | Build web UI and regenerate `wled00/html_*.h` and `wled00/js_*.h` | ~3 s |
| `npm test` | Run the repository test suite | ~40 s |
| `scripts/native-build.sh` | Configure and build the host runtime from `wled00/` | ~10 s |
| `scripts/native-run.sh --help` | Smoke-test the host CLI entry point | ~2 s |
| `scripts/native-test.sh` | Run native build plus CLI/integration smoke tests | ~10 s |
| `npm run dev` | Watch mode and auto-rebuild web assets on file changes | — |

- Run `npm ci` first on fresh clones or when dependencies change.
- Run `npm run build` before validating changes that touch the web UI or native runtime.
- Validate changes with `npm test` and `scripts/native-test.sh`.

For detailed timeouts, troubleshooting, and validation steps, see
[`agent-build.instructions.md`](agent-build.instructions.md).

## Usermod Guidelines

- New custom effects can be added into the `user_fx` usermod. Read the
  [user_fx documentation](https://github.com/wled/WLED/blob/main/usermods/user_fx/README.md).
- Other usermods may be based on the
  [EXAMPLE usermod](https://github.com/wled/WLED/tree/main/usermods/EXAMPLE).
  Never edit the example directly.
- New usermod IDs can be added into
  [wled00/const.h](https://github.com/wled/WLED/blob/main/wled00/const.h#L160).
- Keep usermod activation/build guidance aligned with `Native-Port-Plan.md` as
  bundled-usermod cleanup progresses.

## Project Structure Overview

- Runtime source: `wled00/` (C++)
- Web UI source: `wled00/data/`
- Native build: `CMakeLists.txt` plus `scripts/native-*.sh`
- Auto-generated headers: `wled00/html_*.h` and `wled00/js_*.h`
- Usermods: `usermods/`
- Contributor docs: `docs/`
- CI/CD: `.github/workflows/`

## General Guidelines

- Repository language is English.
- Never edit or commit `wled00/html_*.h` or `wled00/js_*.h`.
- Reuse helpers from `wled00/data/common.js` when editing web UI files.
- When unsure, say so and gather more information rather than guessing.
- Provide references when making analyses or recommendations.
- Highlight user-visible breaking changes and ripple effects.
- Verify feature-flag spelling exactly.
- Match existing code style; no automated linting is configured.

Refer to `docs/cpp.instructions.md`, `docs/web.instructions.md`, and
`docs/cicd.instructions.md` for language-specific and workflow-specific
conventions.

## Pull Request Expectations

- No force-push on open PRs.
- Document what changed and why.
- Describe user-visible impact and testing performed.
