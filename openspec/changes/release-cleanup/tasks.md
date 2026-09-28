# Tasks

## 1. Remove dual builds and the inspector

- [ ] 1.1 Delete the `build-speed-experiment/` directory and the `/build-speed-experiment/` line from `.gitignore`; verify the repository root lists only `build/` as a build tree and `.gitignore` no longer names the experiment directory.
- [ ] 1.2 Delete `src/inspector.cpp` and remove the `pluto_latency_inspector` target, its `cxx_std_17` feature, its `/W4 /WX` options, and the `psapi` link from `CMakeLists.txt`; verify `rg -i "inspector|psapi"` finds no source, CMake, or README reference and CMake configures cleanly.

## 2. Strip inspection code from the plugin

- [ ] 2.1 Remove the `TraceEvent` ring, `kTraceCapacity`, `g_trace_*`, `record_creation_trace`, `flush_creation_trace`, and the `trace_select_map`/`trace_create_data`/`trace_create_data_after`/`trace_create_game`/`trace_create_ladder_game` callbacks and their detours; verify an x86 Release build succeeds and no `TraceEvent`/`creation-trace` symbol remains.
- [ ] 2.2 Remove the `snet_create_game_trace`/`snet_create_ladder_game_trace` wrappers, their signatures, addresses, and `install_trace_call`; keep the `create-data` detour that captures the creation buffer and, after the trampoline returns, calls the Fastest override; verify the override still runs after `0x004A68D0` and its `pushfd`/`pushad` register preservation is intact.
- [ ] 2.3 Remove `validate_image_state`, its `ObservedState` cache, `log_tick_line`, `log_host_executable`, and the watcher polling loop; keep the worker thread that installs the derivation detour and creation hooks off the loader lock and then returns; verify no `observed:`/`waiting:`/`ready:` caller remains.

## 3. Remove environment configuration and routine logging

- [ ] 3.1 Remove every `GetEnvironmentVariableA` reader (`dry_run`, `read_bot_measurement`, `creation_trace_requested`, `creation_force_enabled`, and the `PLUTO_FASTEST_LATENCY_LOG` path override in `open_log`); make the Fastest override and creation-hook installation unconditional; verify `rg "GetEnvironmentVariableA|PLUTO_"` finds no match in `src/`.
- [ ] 3.2 Keep only failure logging: signature/validation mismatches and inaccessible entries, trampoline allocation/protection/jump failures, `override-refused-*` cases, and the fatal worker-thread error; delete the startup, host-executable, installed/armed/validated success, calibration, and creation-trace success lines; verify every remaining `log_line` call site is on a refusal or failure path.
- [ ] 3.3 Update `README.md` to describe only the release surface: one `./build` output, no `PlutoLatencyInspector.exe`, no environment variables, logging only on failure; verify the retained SmartLoader workflow, Fastest override, and rollback sections remain accurate.

## 4. Verify the release build

- [ ] 4.1 Configure and build the x86 Release configuration into `./build`; verify it succeeds and produces `PlutoFastestLatencyFix.dll` with no inspector executable.
- [ ] 4.2 Review the cleaned `src/plugin.cpp` end to end; verify it contains only the derivation detour, the creation-speed override, their signature validation and refusal logging, and the worker-thread startup, and that no live-state observation or environment configuration remains.
