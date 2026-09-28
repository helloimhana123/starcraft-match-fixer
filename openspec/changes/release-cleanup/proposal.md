# Proposal

## Why

The plugin was built up with heavy diagnostic scaffolding: a separate inspector
executable, a second experiment build tree, live state-observation loops, trace
hooks and event buffers, calibration logging, and environment-variable controls.
The fix is now validated, so that scaffolding adds attack surface, maintenance
cost, and routine log noise. The release should be minimal and quiet except when
something fails.

## What Changes

- Remove the standalone diagnostic executable `PlutoLatencyInspector.exe`
  (`src/inspector.cpp`, its CMake target, and all README references). It is not
  part of the runtime and is unused.
- Remove the dual build tree: delete `build-speed-experiment/` and its
  `.gitignore` entry so the project builds only into `./build`.
- Remove plugin code that exists only to inspect the live engine state: the
  `creation-trace` event buffer and flusher, the `select-map`, `create-game`,
  `create-ladder`, `snet-call`, and `snet-ladder` trace hooks, the periodic
  `validate_image_state` observation loop (`observed:`, `diagnostic-only:`,
  `waiting:`, `ready:` lines), `log_host_executable`, and the
  bot-measurement/calibration logging.
- Remove all environment-variable configuration (`PLUTO_FASTEST_LATENCY_LOG`,
  `PLUTO_FASTEST_LATENCY_DRY_RUN`, `PLUTO_FASTEST_LATENCY_TRACE_CREATION`,
  `PLUTO_BOT_MEASURED_LATENCY`, `PLUTO_FASTEST_LATENCY_FORCE_CREATION_SPEED`).
  The pre-lobby Fastest override and the turn-length correction are always
  active.
- Reduce plugin logging to failures only: signature/validation refusals,
  override and detour install failures, and fatal startup errors. A successful
  normal run writes no log line.
- Keep the two validated behaviors and their safety gates: the pre-lobby
  creation-speed Fastest override at create-data `+0x26`, and the derivation-time
  `LatencyFrames[6] = 1` correction, including exact signature validation and
  refusal paths.
- Update `README.md` to describe the release surface only (single build output,
  no inspector, no environment variables, error-only logging).

**BREAKING**: `PlutoLatencyInspector.exe` and the diagnostic environment
variables are removed. Operators can no longer pick a log path, request a
write-disabled dry run, request creation tracing, or supply a bot measurement via
the environment, and the plugin writes no routine log.

## Capabilities

### New Capabilities
- `release-build`: a single build output rooted at `./build` that produces only
  the injectable fix DLL, with no inspector executable and no experiment build
  tree.
- `release-runtime`: the shipped plugin has no environment-variable
  configuration and no live-state inspection code, logs only on failure, and
  retains the validated Fastest override and latency correction.

### Modified Capabilities
- None: `openspec/specs/` currently contains no durable capabilities. This
  change removes the diagnostic behavior described by the in-flight
  `pluto-fastest-latency-fix` change's `latency-diagnostics` delta and narrows
  the per-match observable-record requirements of its `game-turn-latency` delta.
  Those deltas must be reconciled (dropped or rewritten) before either change is
  archived.

## Impact

- `src/plugin.cpp` shrinks substantially; `src/inspector.cpp` and its CMake
  target are deleted; `CMakeLists.txt` and `.gitignore` are updated.
- `build-speed-experiment/` (an untracked, gitignored build output tree) is
  deleted; `build/` remains the sole build directory.
- `README.md` diagnostic, dry-run, and environment-variable sections are
  rewritten for the release.
- No change to the validated memory writes, addresses, SmartLoader loading, or
  network-visible settings. Rollback remains removing the DLL path from the
  SmartLoader profile.
