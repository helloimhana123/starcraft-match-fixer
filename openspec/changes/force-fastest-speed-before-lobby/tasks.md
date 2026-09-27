# Tasks

## 1. Establish the creation boundary and preserve baseline

- [ ] 1.1 Preserve the known-good x86 DLL, host/peer SmartLoader profile entries and session-distinguishable logs; verify the backup loads and the experimental derivation-time speed write can be distinguished from the established `LatencyFrames[6] = 1` correction.
- [ ] 1.2 Trace Local PC `GLUE_CREATE_MULTI` map-OK to native creation/advertisement using `bwapi-main` and the installed 1.16.1 executable; document verified call sites, control/data flow, signatures, and the `SNetCreateGame` boundary, verifying which path actually executes for a host without mistaking symbol-map names for runtime proof.
- [ ] 1.3 Identify the authoritative pre-advertisement speed value by comparing Slowest and Fastest creation paths and, if static tracing is ambiguous, a read-only runtime trace/watchpoint; verify the source, copies, index range, last mutating point and host-only lifecycle across consecutive games before authorizing a write. If no safe point is verified, stop without implementing a guessed override.

## 2. Implement a validated host-only override

- [ ] 2.1 Remove or disable only the failed derivation-time `GameSpeed` write in `src/plugin.cpp`, retaining the validated Fastest turn-table correction; verify x86 Release build and review show no late speed write and no change to network latency or speed-modifier tables.
- [ ] 2.2 Add a one-time-installed, guarded host-creation hook at the **verified** D1 boundary that sets the authoritative speed to index `6` before room advertisement, no-ops for `6`, and refuses unsupported signature/path/value; verify x86 Release build, source review, and a controlled non-Fastest creation readback. Do not touch the UI slider, joiner creation state, or on-disk game image.
- [ ] 2.3 Record per-creation timestamps, original and final creation speed, applied/no-op/refused outcome and refusal reason, with first-gameplay observations separated by client; verify logs distinguish an applied override, already-Fastest case, unsupported-state refusal, and subsequent automatic match.

## 3. Validate both clients and match behavior

- [ ] 3.1 With host's prior setting deliberately Slowest or Normal, test an experimental DLL in controlled SmartLoader profiles and record host and peer lobby speed **before game start**; verify both display Fastest and correlate with host creation log, or fail validation without claiming a successful room override.
- [ ] 3.2 Verify host and peer first-gameplay and continuing speed index, measured Fastest frame pacing, corrected Fastest turn length/scheduler, unchanged network latency and pluto's four-frame accepted probe; if either client differs or the probe rejects, fail validation and restore the known-good DLL.
- [ ] 3.3 Repeat with prior host setting already Fastest, then complete a full two-client match and a second automatically hosted game; verify both lobbies and both games stay Fastest with no unnecessary speed write, desync, timeout, player drop or latency regression.

## 4. Record outcome and rollback

- [ ] 4.1 Record verified and refused cases, both clients' lobby and gameplay observations, measured pacing, bot verdict, full-match result, consecutive creation and any omitted checks in this change's evidence; verify claims do not rely on local readback alone and do not silently treat the active derivation-detour change as complete.
- [ ] 4.2 Reconcile the overlapping in-progress `game-speed-selection` delta before syncing/archiving either change, documenting whether the older Normal-lobby acceptance is superseded; verify there is one coherent speed-selection contract rather than two contradictory successful claims.
- [ ] 4.3 Test rollback by disabling the experimental DLL in both SmartLoader profiles or restoring the known-good build; verify fresh stock/known-good launches produce no new pre-lobby application records and follow normal saved-speed behavior.
