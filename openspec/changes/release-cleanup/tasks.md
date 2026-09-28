# Tasks

## 1. Remove dual builds and the inspector, rename the artifact

- [x] 1.1 Delete the `build-speed-experiment/` directory and the `/build-speed-experiment/` line from `.gitignore`; verify the repository root lists only `build/` as a build tree and `.gitignore` no longer names the experiment directory.
- [x] 1.2 Delete `src/inspector.cpp` and remove the `pluto_latency_inspector` target, its `cxx_std_17` feature, its `/W4 /WX` options, and the `psapi` link from `CMakeLists.txt`; verify `rg -i "inspector|psapi"` finds no source, CMake, or README reference and CMake configures cleanly.
- [x] 1.3 Rename the CMake `project()` and the shared-library target to `matchfixer` and set `OUTPUT_NAME "MatchFixer"` (keep `PREFIX ""`); verify an x86 Release configure and build produces `MatchFixer.dll` and no `PlutoFastestLatencyFix.dll`.

## 2. Strip inspection code from the plugin

- [x] 2.1 Remove the `TraceEvent` ring, `kTraceCapacity`, `g_trace_*`, `record_creation_trace`, `flush_creation_trace`, and the `trace_select_map`/`trace_create_data`/`trace_create_data_after`/`trace_create_game`/`trace_create_ladder_game` callbacks and their detours; verify an x86 Release build succeeds and no `TraceEvent`/`creation-trace` symbol remains.
- [x] 2.2 Remove the `snet_create_game_trace`/`snet_create_ladder_game_trace` wrappers, their signatures, addresses, and `install_trace_call`; keep the `create-data` detour that captures the creation buffer and, after the trampoline returns, calls the speed override; verify the override still runs after `0x004A68D0` and its `pushfd`/`pushad` register preservation is intact.
- [x] 2.3 Remove `validate_image_state`, its `ObservedState` cache, `log_tick_line`, `log_host_executable`, and the watcher polling loop; keep the worker thread that installs the derivation detour and creation hooks off the loader lock and then returns; verify no `observed:`/`waiting:`/`ready:` caller remains.
- [x] 2.4 Change the default log path in `open_log` from `PlutoFastestLatencyFix.log` to `MatchFixer.log`; verify no `PlutoFastestLatencyFix` string remains in `src/`.

## 3. Replace environment configuration with MatchFixer.ini and keep failure-only logging

- [x] 3.1 Remove every `GetEnvironmentVariableA` reader (`dry_run`, `read_bot_measurement`, `creation_trace_requested`, `creation_force_enabled`, and the `PLUTO_FASTEST_LATENCY_LOG` path override in `open_log`); verify `rg "GetEnvironmentVariableA|PLUTO_"` finds no match in `src/`.
- [x] 3.2 Add a `MatchFixer.ini` reader that opens `MatchFixer.ini` in the client working directory and reads the integer keys `GameSpeed` and `LatencyFrames` with the Win32 profile API (which ignores `;` comment lines), using a sentinel default outside each valid range so absent or unparseable values are detected; provide no built-in defaults.
- [x] 3.3 Gate each fix step independently and log a refusal before skipping:
  - the pre-lobby creation-speed override applies only when `GameSpeed` is present and in `0..6`, and forces create-data `+0x26` to that value;
  - the derivation-time correction applies only when `LatencyFrames` is present and in `1..20`, and writes that value to every entry of the latency-frame table at `0x0051CE70` after the original derivation;
  - a missing file or a missing/out-of-range key means that step performs no write and logs the reason. Verify no step writes when `MatchFixer.ini` is absent.
- [x] 3.4 Keep only failure logging: signature/validation mismatches and inaccessible entries, trampoline allocation/protection/jump failures, missing or invalid configuration, override refusal cases, and the fatal worker-thread error; delete the startup, host-executable, installed/armed/validated success, calibration, and creation-trace success lines; verify every remaining `log_line` call site is on a refusal or failure path.
- [x] 3.5 Update `README.md` to describe only the release surface: one `./build` output named `MatchFixer.dll`, no `PlutoLatencyInspector.exe`, no environment variables, the `MatchFixer.ini` keys and ranges with no defaults, and logging only on failure; verify the retained SmartLoader workflow, configured speed override, and rollback sections remain accurate.

## 4. Verify the release build

- [x] 4.1 Configure and build the x86 Release configuration into `./build`; verify it succeeds and produces `MatchFixer.dll` with no inspector executable.
- [x] 4.2 Review the cleaned `src/plugin.cpp` end to end; verify it contains only the derivation detour, the creation-speed override, the `MatchFixer.ini` reader, their signature validation and refusal logging, and the worker-thread startup, and that no live-state observation or environment configuration remains.
