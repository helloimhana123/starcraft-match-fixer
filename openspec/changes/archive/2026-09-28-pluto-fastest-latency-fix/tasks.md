# Tasks

## 1. Ground truth and calibration

- [x] 1.3 Record the calibration evidence in the change. Clean
      write-disabled comparison shows Normal's selected table and scheduler
      values of 1 produce the accepted 4-frame verdict, while Fast/Faster/Fastest
      values of 2 produce rejected 6-frame verdicts; direct initialized writes
      are rejected as session-unsafe. Verify: evidence is recorded in `design.md`.

## 2. Delivery mechanism

- [x] 2.1 Create the 32-bit self-memory client DLL skeleton: a watcher thread
      started from `DllMain`, host executable validation, and guarded access to
      the StarCraft process's own memory. Verify: the DLL builds for x86 and
      records a startup or refusal line without blocking the host.
- [x] 2.2 Integrate the self-memory plugin with SmartLoader: write the built plugin's DLL
      path to `C:\Starcraft\mods.BWAPI.txt` and launch
      `C:\Starcraft\StarCraft-SL.pluto.exe`. The available SmartLoader launch
      record is `C:\Starcraft\SmartLoader.pluto.log`. Verify that this log
      shows the DLL load and the plugin log shows host validation, without
      requiring any separate injector or helper.
- [x] 2.3 Configure SmartLoader for peer diagnostics using the same plugin.
      Verify: `StarCraft-SL.exe` loads the DLL from `mods.txt` and the peer log
      records validated memory state. Direct timing writes remain disabled.

## 3. Patch logic

- [x] 3.1 Implement signature validation with logging and no writes: the host
      must be a validated StarCraft executable, the speed table at `0x005124D8`
      must equal `{167,111,83,67,56,48,42}`, the speed index at `0x006CDFD4`
      must be in range, and the observed latency frame must be valid. Verify:
      a dry-run logs the observed values and a clear verdict with no writes.
- [x] 3.2 Retire the direct live scheduler write path and keep correction writes
      disabled pending a validated pre-initialization mechanism. Verify: both
      SmartLoader profiles produce only timestamped diagnostic records and the
      compiled DLL contains no game-speed, timing, or network-latency write path.
- [x] 3.3 Implement continuous observation and a guarded re-application path for
      a future validated target. Verify: in a two-match diagnostic session, the
      log records both opening timing-state transitions without writes; enable
      application only after a pre-initialization mechanism is validated.

## 6. Pre-initialization investigation

- [x] 6.1 Identify and document the validated 1.16.1 derivation or initialization
      boundary that copies per-speed timing into the active scheduler state.
      Static inspection identified the derivation loop at `0x004D92A0` and its
      table clear/store range at `0x004D92FD–0x004D9354`. Read-only correlation
      observed initialized state at frame 0 before pluto issued commands at frame
      6, without changing client memory.
- [x] 6.2 Capture the exact validated 1.16.1 instruction signature at the
      derivation entry `0x004D92A0` and define the minimum overwrite length for
      an in-memory detour. The signature is `55 8B EC 83 EC 24`, the overwrite
      length is 6 bytes, and the trampoline resumes at `0x004D92A6`. Verify: an
      unexpected byte sequence causes a refusal before any detour installation.
- [x] 6.3 Implement an in-memory, reversible derivation-time detour and
      trampoline. Verify: it validates the signature before installation,
      restores page protection, and flushes the instruction cache. The detour
      remains installed for the StarCraft process lifetime and is never removed
      from `DllMain`; process termination reclaims its memory safely.
- [x] 6.4 Preserve original derivation and set only `LatencyFrames[6] = 1` after
      the trampoline returns, before frame 0 completes. Verify: diagnostics show
      the original derivation ran, Fastest table value is 1 at frame 0, and no
      write is made to `GameSpeed`, `GameSpeedModifiers`, network latency, or
      live scheduler field `0x0051CEA0`. Verified: the bot recorded table and
      scheduler values of 1 at frame 0 and pluto logged four 4-frame samples.
## 5. Packaging and documentation

- [x] 5.1 Add a build entry point that produces the self-memory plugin DLL in
      one step. Verify: from a clean checkout on the documented x86 toolchain,
      one command produces the DLL without a separate inspector executable,
      PE rewriting, executable generation, or an external injector.
- [x] 5.2 Document the SmartLoader play workflow, the plugin DLL path in
      `C:\Starcraft\mods.BWAPI.txt`, the SmartLoader log at
      `C:\Starcraft\SmartLoader.pluto.log`, the
      `C:\Starcraft\StarCraft-SL.pluto.exe` launch path, the log location, the
      plugin's diagnostic log as the inspection output, the validated client
      build, and rollback procedure. Verify: following the documented steps
      reproduces a validated match, and the documented plugin name,
      registration, and log output match the artifacts actually produced.
- [x] 5.3 Document the known behavioural difference from a stock match: the target
      speed's turn period is shorter than stock because that is the value the bot
      requires. Verify: the limitation is stated next to the install instructions,
      including what the operator should expect to notice and not notice.
