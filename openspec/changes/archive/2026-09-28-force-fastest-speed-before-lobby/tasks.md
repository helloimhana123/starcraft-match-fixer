# Tasks

## 1. Establish the creation boundary and preserve baseline

- [x] 1.1 Known-good DLL backed up by the operator (installed copy at `C:\Starcraft\PlutoFastestLatencyFix.dll` is swapped only for controlled runs); `mods.pluto-server.txt` and `mods.pluto-client.txt` remain the active entries; the test script writes timestamped per-client logs. The source has no derivation-time `GameSpeed` write, so the `LatencyFrames[6] = 1` correction is logged independently as `derivation:`.
- [x] 1.2 Traced and documented from install to room advertisement; see `design.md` "Diagnostic trace experiment" and both operator-run sections. The host path is `gluCustmLoadMapFromList -> SelectMapOrEntry (0x004A8050) -> 0x004A68D0 -> CreateLadderGame (0x004D3910) -> SNetCreateLadderGame (0x004D3B0B)`; `CreateGame`/`SNetCreateGame` are the non-ladder alternatives. Signatures are recorded in `src/plugin.cpp`.
- [x] 1.3 The authoritative pre-advertisement value is the create-data speed byte at buffer `+0x26`, written by `0x004A68D0` from the chosen speed (`0x004A699C`) and passed onward to the Storm create call. Slowest and Fastest host runs both showed the value surviving into `snet-ladder-before` and matching both clients' lobby. Host-only is evidenced by the client logs never reaching the creation hooks. Consecutive-in-one-process lifecycle is deliberately re-checked during 3.3 before any promotion.

## 2. Implement a validated host-only override

- [x] 2.1 Source review confirms there is no late `GameSpeed` write; only `LatencyFrames[6] = 1` is written after the derivation trampoline, and the speed modifier / network latency tables are only read. x86 Release build succeeds.
- [x] 2.2 Added a signature-gated, one-time-installed host-creation override: after `0x004A68D0` returns, it validates the creation buffer's `+0x26` byte, writes `6` when the original is `0..5`, no-ops on `6`, and refuses inaccessible or out-of-range data with distinct log stages. It never touches the UI slider, joiner state, `GameSpeed`, or on-disk files. Verified at runtime: Slowest host logged `override-applied argument=0 result=6 field26=6` before a successful `SNetCreateLadderGame`.
- [x] 2.3 Each creation emits a deferred `creation-trace:` event with timestamp, thread, process, creation number, buffer, original value, final readback, and outcome; verbose stage tracing stays behind `PLUTO_FASTEST_LATENCY_TRACE_CREATION`. `override-applied` is verified in the 2026-09-27 host log; already-Fastest `override-noop-fastest` and refusal remain to be exercised in 3.3.

## 3. Validate both clients and match behavior

- [x] 3.1 Slowest prior setting tested in controlled profiles: the host log shows the override applied before room creation and the operator confirmed **both** clients displayed Fastest in the lobby before game start.
- [x] 3.2 Host logged `frame=0 speed=6 table_turn=1 scheduler_turn=1 network_latency=0 speed_modifiers=167,111,83,67,56,48,42 latency_frames=1,1,1,1,2,2,1`; peer logged `frame=1 speed=6` with the same tables; `pluto.log` recorded four `4 frames` samples accepted (host won) with unchanged network latency. Both clients agree and the probe passed.
- [ ] 3.3 Repeat with prior host setting already Fastest, then complete a full two-client match and a second automatically hosted game; verify both lobbies and both games stay Fastest with no unnecessary speed write, desync, timeout, player drop or latency regression. Partial: a second creation in the same process re-applied the override successfully, but its match was not confirmed.

## 4. Record outcome and rollback

- [ ] 4.1 Record verified and refused cases, both clients' lobby and gameplay observations, measured pacing, bot verdict, full-match result, consecutive creation and any omitted checks in this change's evidence; verify claims do not rely on local readback alone and do not silently treat the active derivation-detour change as complete. Partial: the verified Slowest-host case, both clients' lobby/gameplay observations, pacing and Pluto verdict are recorded in `design.md`; the already-Fastest case, confirmed second match and refusal case remain open in 3.3.
- [x] 4.2 Reconciled: the older `game-speed-selection` delta's Normal-lobby acceptance is marked superseded in that change's `spec.md` and `design.md`, and noted in this change's `design.md`. One coherent contract remains: pre-lobby Fastest via the verified `+0x26` creation byte.
- [ ] 4.3 Test rollback by disabling the experimental DLL in both SmartLoader profiles or restoring the known-good build; verify fresh stock/known-good launches produce no new pre-lobby application records and follow normal saved-speed behavior.
