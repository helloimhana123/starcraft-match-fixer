# Tasks

## 1. Ground truth and calibration

- [ ] 1.1 Add log-based calibration output to the in-process plugin. It must
      report the game speed index, the millisecond speed table, the per-speed
      turn-length table, the latency setting, and the current turn length using
      the validated 1.16.1 addresses `GameSpeed = 0x006CDFD4`,
      `GameSpeedModifiers = 0x005124D8`, `LatencyFrames = 0x0051CE70`, and
      `Latency = 0x006556E4`. Verify: launch the plugin with writes disabled,
      confirm its log contains the expected millisecond table
      (167, 111, 83, 67, 56, 48, 42), and confirm it reports a turn length of
      5 at Fastest and 3 at Normal. Record any refusal or unsupported-host
      result in the same log.
- [ ] 1.2 Confirm the engine-to-bot latency mapping empirically. Verify: one match
      per lobby speed for Fastest, Faster, Fast, Normal and Slow, reading the
      bot's own recorded measured latency each time, and confirm the observed
      values match the prediction (6 for Fastest, 4 for Normal) and that the bot
      only accepts speeds whose turn length is 3.
- [ ] 1.3 Freeze the calibration as a recorded decision in the change (forced
      speed = Fastest, target turn length = 3, with the evidence that the bot's
      measured value equals the turn length plus one). Verify: the decision and
      its evidence are written down, and the chosen default is traceable to task
      1.1 and 1.2 output rather than to assumption.

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
- [ ] 2.3 Document and, if required by task 4.2, configure SmartLoader for the
      peer client using the same plugin. Verify: the peer client launches as a
      normal client and its plugin log line appears. This task is conditional on
      task 4.2 failing - do not enable peer patching while the bot-only
      configuration is stable.

## 3. Patch logic

- [x] 3.1 Implement signature validation with logging and no writes: the host
      must be a validated StarCraft executable, the speed table at `0x005124D8`
      must equal `{167,111,83,67,56,48,42}`, the speed index at `0x006CDFD4`
      must be in range, and the observed latency frame must be valid. Verify:
      a dry-run logs the observed values and a clear verdict with no writes.
- [x] 3.2 Implement the self-memory write path: write `GameSpeed = 6` at
      `0x006CDFD4`, then write `LatencyFrames[6] = 3` at `0x0051CE70` and read
      both values back. Do not write `Latency` at `0x006556E4`. Verify: the log
      records the previous and applied values and matching read-backs.
- [x] 3.3 Implement continuous re-application and per-match logging. Verify: in a
      two-match session started back-to-back, the log shows an application in
      both matches, and the second match's value is correct before the bot's
      latency probe reports.

## 4. Validation

- [ ] 4.1 Validate the bot's verdict. Verify: in a match at Fastest with the fix
      active, the bot's log reports an accepted action latency (4 frames) and does
      not report a mismatch, and the fix's log confirms the applied value.
- [ ] 4.2 Validate match integrity with an unmodified peer. Verify: a full match
      completes with no desync, drop, or "player not responding", and the peer's
      room settings are identical to a stock match at the same game speed. If this
      fails, task 2.3 becomes required and this task is re-run with both clients
      patched.
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
