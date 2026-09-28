# Design

## Context

See `proposal.md`. The current state that shapes the work:

- `src/plugin.cpp` (899 lines) mixes the two shipping behaviors with
  investigation scaffolding: a 2048-entry `TraceEvent` ring and flusher, trace
  detours on `select-map`/`create-game`/`create-ladder` and trace call patches on
  `snet-call`/`snet-ladder`, a `create-data` detour that both captures the
  creation buffer and traces, a periodic `validate_image_state` loop with change
  detection and calibration output, `log_host_executable`, and env-var readers
  that the release replaces with a `MatchFixer.ini` reader.
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
  (`MatchFixer.dll`).
- Make the release surface match the README: no inspector, no environment
  variables, configuration only from `MatchFixer.ini`, no routine log.

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

Keep `open_log`/`log_line` (defaulting to `MatchFixer.log`) and
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

### D4: Remove the observation loop and all env-var readers; configure from MatchFixer.ini

Delete `validate_image_state`, its `ObservedState` caching, and all
`GetEnvironmentVariableA` readers (`dry_run`, `read_bot_measurement`,
`creation_trace_requested`, `creation_force_enabled`, and the log-path override
in `open_log`). Configuration moves to `MatchFixer.ini` (D7); there is no
environment input. The creation hooks are installed when `GameSpeed` is valid and
the derivation correction runs when `LatencyFrames` is valid, so `g_running` is
no longer needed for a polling loop. The worker thread still installs the
derivation detour and creation hooks off the loader lock, then returns; the
detour stays for the process lifetime as before.

### D5: Single build tree and MatchFixer rename

Delete the `build-speed-experiment/` directory and the
`/build-speed-experiment/` line in `.gitignore`. Rename the CMake `project()` and
the shared-library target to `matchfixer` and set `OUTPUT_NAME MatchFixer`
(keeping `PREFIX ""`), so `./build` produces `MatchFixer.dll` and no
`PlutoFastestLatencyFix.dll`. The default plugin log name changes to
`MatchFixer.log` (D3). No CMake preset or script changes are needed because no
script selects a build directory.

### D6: README reflects the release surface

Rewrite the diagnostic-mode, trace, dry-run, and bot-measurement sections, rename
all artifact references to `MatchFixer.dll`/`MatchFixer.log`, and document the
`MatchFixer.ini` keys and ranges with no defaults. Retain the fix description,
the SmartLoader workflow, the speed override behavior, and the rollback
instructions, updated to state that logging occurs only on failure and no
environment variable is read.

### D7: Configuration from MatchFixer.ini, with no built-in defaults

The plugin reads two integer settings from `MatchFixer.ini` in the client working
directory with the Win32 profile API, which ignores `;` comment lines. Each key
is read with a sentinel default outside its valid range so an absent or
unparseable value is detected rather than silently substituted. There is no
built-in fallback.

- `GameSpeed` (`0..6`): the in-game speed forced into the pre-lobby creation
  buffer at `+0x26`.
- `LatencyFrames` (`1..20`): the value written to every entry of the per-speed
  latency-frame table at `0x0051CE70` after the derivation routine runs.

The two steps are independent. A step applies only when its key is present and in
range; otherwise the step performs no write and logs a refusal. If the file is
missing, neither applies. The file is optional at the OS level but the fix is
inactive without it.

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
- **A missing or mistyped `MatchFixer.ini` disables a fix step** -> this is the
  requested no-defaults behavior; the affected step logs a refusal so the
  operator can tell the fix did not apply.
- **Dry-run and log-path controls are lost for future experiments** -> accepted
  for the release; a development branch can reintroduce them if needed.
- **Deleting `build-speed-experiment/` removes an operator's build** -> it is
  untracked CMake output and can be regenerated; no source is lost.

## Migration Plan

1. Rename the CMake project/target and output to `matchfixer`/`MatchFixer`, delete
   `src/inspector.cpp`, `build-speed-experiment/`, and their CMake and
   `.gitignore` references.
2. Strip the diagnostic code and env-var readers from `src/plugin.cpp`, add the
   `MatchFixer.ini` reader, and gate the two validated behaviors on valid
   configuration.
3. Build the x86 Release DLL into `./build` and confirm it is produced alone as
   `MatchFixer.dll`.
4. Confirm review shows no `log_line` on a success path, no
   `GetEnvironmentVariableA` call, and that both fix steps skip and log when
   `MatchFixer.ini` is absent.
5. Rollback: revert this change; the previous commit still has the inspector and
   diagnostics.
