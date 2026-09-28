# Design

## Context

See `proposal.md`. The current state that shapes the work:

- `src/plugin.cpp` (899 lines) mixes the two shipping behaviors with
  investigation scaffolding: a 2048-entry `TraceEvent` ring and flusher, trace
  detours on `select-map`/`create-game`/`create-ladder` and trace call patches on
  `snet-call`/`snet-ladder`, a `create-data` detour that both captures the
  creation buffer and traces, a periodic `validate_image_state` loop with change
  detection and calibration output, `log_host_executable`, and env-var readers.
- `src/inspector.cpp` is a separate read-only external process tool built as
  `PlutoLatencyInspector.exe`.
- Two build trees exist: `build/` and `build-speed-experiment/` (both are
  gitignored). Only `build/` is referenced going forward.
- The validated behaviors to preserve: the Fastest pre-lobby override written to
  the creation buffer at `+0x26` after `0x004A68D0` returns, and the
  process-lifetime derivation detour at `0x004D92A0` that sets
  `LatencyFrames[6] = 1` after the original derivation runs. Both are gated by
  exact signature validation and must keep refusing on mismatch.

## Goals / Non-Goals

**Goals:**
- Reduce the shipped DLL to the two validated writes plus their validation and
  refusal paths, with failure-only logging.
- Leave one build entry point (`./build`) and one runtime artifact
  (`PlutoFastestLatencyFix.dll`).
- Make the release surface match the README: no inspector, no environment
  variables, no routine log.

**Non-Goals:**
- Changing addresses, hook targets, write semantics, or the SmartLoader launch
  path.
- Adding automated tests or a build script; the project has no test harness and
  validation remains a manual two-client run.
- Removing the worker thread or changing how the DLL installs its detours
  (loader-lock safety still applies).

## Decisions

### D1: Delete the inspector rather than leave it unbuilt

Remove `src/inspector.cpp`, its `pluto_latency_inspector` CMake target, the
`psapi` link, its compile features/options, and README instructions. Alternatives
considered: keep the source and exclude it from the default build; that leaves
dead code and a second artifact to reason about at release time for no runtime
benefit, and the in-process plugin already replaces its purpose.

### D2: Keep only the hooks the fix needs

Remove the `select-map`, `create-game`, `create-ladder`, `snet-call`, and
`snet-ladder` trace hooks and the `snet_create_game_trace`/ladder wrappers, along
with the whole `TraceEvent` ring, `record_creation_trace`, `flush_creation_trace`,
`trace_*` callbacks, and the trace-enabling reader. Keep the `create-data` detour
because the override is applied only after `0x004A68D0` returns; it now does just
two things: capture the creation buffer from `EAX` (needed by the override),
call the original through the trampoline, then call the override. The creation
buffer is held in the existing `g_create_data_buffer` atomic instead of a trace
event.

### D3: Error-only logging through the existing `log_line`

Keep `open_log`/`log_line` (defaulting to `PlutoFastestLatencyFix.log`) and
delete every non-failure caller: the startup and host-executable lines, the
installed/armed/validated success lines, the `observed:` block, the
`diagnostic-only:` line, `log_tick_line`, `read_bot_measurement`, and the
calibration lines. The remaining calls sit on refusal and failure paths only:

- `validate_trace_signature` / `validate_derivation_signature` mismatch or
  inaccessible entry
- trampoline allocation, protection-change, or out-of-range jump failures
- `override-refused-*` cases (no buffer, inaccessible, out of range, failed
  write)
- the fatal worker-thread creation error in `DllMain`

The `creation-trace:` success records are deleted; the Fastest override applied
successfully is silent. The function formerly named `record_creation_trace` on
failure paths is replaced by direct `log_line` calls with the same reason text.

### D4: Remove the observation loop and all env-var readers

Delete `validate_image_state`, its `ObservedState` caching, and all
`GetEnvironmentVariableA` readers (`dry_run`, `read_bot_measurement`,
`creation_trace_requested`, `creation_force_enabled`, and the log-path override
in `open_log`). With `PLUTO_FASTEST_LATENCY_FORCE_CREATION_SPEED` gone the
override is always armed, so the creation hooks are installed unconditionally and
`g_running` is no longer needed for a polling loop. The worker thread still
installs the derivation detour and creation hooks off the loader lock, then
returns; the detour stays for the process lifetime as before.

### D5: Single build tree

Delete the `build-speed-experiment/` directory and the
`/build-speed-experiment/` line in `.gitignore`. `CMakeLists.txt` keeps one
target, `pluto_fastest_latency_fix`, built in `./build` with
`OUTPUT_NAME PlutoFastestLatencyFix` and the x86 Release configuration. No CMake
preset or script changes are needed because no script selects a build directory.

### D6: README reflects the release surface

Rewrite the diagnostic-mode, trace, dry-run, and bot-measurement sections.
Retain the fix description, the SmartLoader workflow, the Fastest override
behavior, and the rollback instructions, updated to state that logging occurs
only on failure and no environment variable is read.

## Risks / Trade-offs

- **Removing a hook the override depends on** -> the `create-data` detour is kept
  and the override still runs after the trampoline returns; verify by building
  and reviewing every `log_line` call site is a failure path.
- **Clobbering the original function's return value** -> the override call stays
  wrapped in `pushfd`/`pushad`/`popfd`/`popad` so `EAX` and flags survive to the
  host caller, as in the current detour.
- **Error-only logging hides a real problem** -> refusals still log when the file
  can be opened, and a failed log open is the same trade-off the current build
  accepts; the fix's internal state is unchanged.
- **Dry-run and log-path controls are lost for future experiments** -> accepted
  for the release; a development branch can reintroduce them if needed.
- **Deleting `build-speed-experiment/` removes an operator's build** -> it is
  untracked CMake output and can be regenerated; no source is lost.

## Migration Plan

1. Delete `src/inspector.cpp`, `build-speed-experiment/`, and their CMake and
   `.gitignore` references.
2. Strip the diagnostic code and env-var readers from `src/plugin.cpp`, keeping
   the two validated behaviors and their signature checks.
3. Build the x86 Release DLL into `./build` and confirm it is produced alone.
4. Confirm review shows no `log_line` on a success path and no
   `GetEnvironmentVariableA` call.
5. Rollback: revert this change; the previous commit still has the inspector and
   diagnostics.
