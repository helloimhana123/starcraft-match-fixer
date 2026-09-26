# Tasks

## 1. Ground truth and calibration

- [ ] 1.1 Add log-based calibration output to the in-process plugin. It must
      report the game speed index, the millisecond speed table, the per-speed
      turn-length table, the latency setting, and the current turn length using
      the validated 1.16.1 addresses `GameSpeed = 0x006CDFD4`,
      `GameSpeedModifiers = 0x005124D8`, `LatencyFrames = 0x0051CE70`, and
      `Latency = 0x006556E4`. Verify: launch the plugin with writes disabled,
      confirm its log contains the expected millisecond table
      (167, 111, 83, 67, 56, 48, 42), records the full raw latency table through
      the opening probe, and records any refusal or unsupported-host result in
      the same log. The expected raw turn-length values are not assumed.
- [ ] 1.2 Confirm the engine-to-bot latency mapping empirically. Verify: one match
      per lobby speed for Fastest, Faster, Fast, Normal and Slow, reading the
      bot's own recorded measured latency each time, pairing it with all
      diagnostic samples during the opening probe, and identify a predictive
      engine-state relationship. Record a failed hypothesis explicitly; do not
      enable a write target unless the relationship is validated.
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
- [ ] 3.2 Retire the direct live scheduler write path and keep correction writes
      disabled pending a validated pre-initialization mechanism. Verify: both
      SmartLoader profiles produce only timestamped diagnostic records and no
      timing, game-speed, or network-latency writes.
- [x] 3.3 Implement continuous observation and a guarded re-application path for
      a future validated target. Verify: in a two-match diagnostic session, the
      log records both opening timing-state transitions without writes; enable
      application only after a pre-initialization mechanism is validated.

## 6. Pre-initialization investigation

- [ ] 6.1 Identify and document the validated 1.16.1 derivation or initialization
      boundary that copies per-speed timing into the active scheduler state.
      Verify: read-only evidence identifies the relevant code path and its order
      relative to pluto's opening probe without changing client memory.
- [ ] 6.2 Design and validate a pre-initialization correction mechanism only if
      task 6.1 identifies one that preserves two-client match integrity. Verify:
      a controlled Fastest match completes without a drop or desync and pluto
      reports a 4-frame accepted latency.

## 4. Validation

- [ ] 4.1 Validate the bot's verdict after task 6.2 authorizes a correction.
      Verify: in a match at Fastest with the authorized fix active, the bot's log
      reports an accepted action latency (4 frames) and does not report a mismatch,
      and the fix's log confirms the calibrated applied value.
- [ ] 4.2 Validate match integrity after task 6.2 authorizes a correction.
      Prior validation failed: initialized writes on both clients dropped the AI.
      Verify: a full match completes with no desync, drop, or "player not
      responding", and peer room settings match stock.
- [ ] 4.3 Validate that nothing else changed. Verify: the local client is forced
      to Fastest, its frame pacing matches Fastest behavior, the network-visible
      latency and room settings are unchanged, and no `bwapi.ini` key differs
      from the pre-change state.
- [ ] 4.4 Validate rollback. Verify: removing or disabling the plugin DLL path
      in `C:\Starcraft\mods.BWAPI.txt`, clearing or separating the previous
      `C:\Starcraft\SmartLoader.BWAPI.log` evidence, and launching the stock
      client produces no new fix log and stock behavior at Fastest, with the
      plugin's files left in place.

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
