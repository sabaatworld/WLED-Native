---
applyTo: "**"
---
# Agent-Mode Build & Test Instructions

Detailed build workflow, timeouts, and troubleshooting for agent-mode changes.
During the native macOS/Linux migration, `Native-Port-Plan.md` remains the
source of truth for scope and status.

## Build Timing and Timeouts

| Command | Typical Time | Minimum Timeout | Notes |
|---|---|---|---|
| `npm run build` | ~3 s | 30 s | Web UI -> `wled00/html_*.h` and `wled00/js_*.h` |
| `npm test` | ~40 s | 2 min | Repository tests via `node --test` |
| `scripts/native-build.sh` | ~10 s | 2 min | Configure and build the native runtime in `build/native/` |
| `scripts/native-run.sh --help` | ~2 s | 30 s | Smoke-test the host CLI entry point |
| `scripts/native-test.sh` | ~10 s | 2 min | Native build plus CLI/integration smoke tests |
| `npm run dev` | continuous | — | Watch mode, auto-rebuilds on changes |

## Development Workflow

### Code Style Summary

- C++ files in `wled00/` and `usermods/`: 2-space indentation, camelCase
  functions/variables, PascalCase classes, UPPER_CASE macros.
- Web UI files in `wled00/data`: tabs for indentation.
- GitHub Actions workflows in `.github/workflows`: 2-space indentation and
  explicit `name:` fields on workflows, jobs, and steps.

### Web UI Changes

1. Edit files in `wled00/data/`.
2. Run `npm run build`.
3. Test with a local HTTP server if the change is browser-visible.
4. Run `npm test`.

### Runtime Changes

1. Edit files in `wled00/` but never `html_*.h` or `js_*.h`.
2. Run `npm run build`.
3. Run `scripts/native-build.sh`.
4. Run `scripts/native-test.sh`.

## Before Finishing Work - Testing

You must complete all of these before marking work as done:

1. Run `npm test`.
2. Run `scripts/native-build.sh`.
3. Run `scripts/native-test.sh`.
4. For web UI changes, manually test the interface.

If any step fails, fix the issue or clearly report the blocker.

## Manual Web UI Testing

Start a local server:

```sh
cd wled00/data && python3 -m http.server 8080
# Open http://localhost:8080/index.htm
```

Check page load, console errors, navigation, controls, and any changed settings
or API interactions.

## Troubleshooting

| Problem | Solution |
|---|---|
| Missing `html_*.h` | Run `npm ci; npm run build` |
| Web UI looks broken | Check browser console for JS errors |
| Node.js version mismatch | Ensure Node.js 20+ (check `.nvmrc`) |

### Recovery Steps

- Force web UI rebuild: `npm run build -- -f`
- Clear generated files: `rm -f wled00/html_*.h wled00/js_*.h && npm run build`
- Clean native build artifacts: `rm -rf build/native`
- Reinstall Node deps: `rm -rf node_modules && npm ci`

## CI/CD Validation

The active GitHub Actions CI workflow runs:

1. `npm run build`
2. `npm test`
3. `scripts/native-build.sh`
4. `scripts/native-test.sh`

Match that flow locally before claiming success.
