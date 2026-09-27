# Tasks

## 1. Preserve the working baseline

- [x] 1.1 Confirm the operator's backup of the current working DLL and preserve baseline host/peer plugin logs, pluto's four-frame verdict and both active SmartLoader profile entries before experimenting. Verify the backup is accessible and the existing `speed=6`, table/scheduler turn `1`, and accepted probe can be identified without conflating old and new sessions; do not require a separately recorded Fastest lobby.
- [x] 1.2 Establish a deliberately non-Fastest host starting point without the experimental DLL: set the host's previous lobby selection to Normal, let BWAPI auto-host, and record the speed displayed on both clients before gameplay. Verify the Normal lobby is recorded as a baseline; its display text is not an acceptance criterion for the corrected match.

## 2. Extend the validated detour

- [x] 2.1 In `src/plugin.cpp`, add a guarded read of `GameSpeed` within the original derivation callback's existing detour, validate index `0..6` and expected engine state, write index `6` only when needed, and preserve the `LatencyFrames[6] = 1` correction. Verify an x86 Release build succeeds and review confirms no writes to the speed-modifier table, network latency, or live scheduler.
- [ ] 2.2 Add per-derivation and frame-0 diagnostics for prior speed, write/refusal/no-op, readback speed, corrected table and scheduler state, and failure reason; keep per-client logs distinguishable. Verify controlled runs show both applied and already-Fastest cases and an unsupported signature/state fails closed without a new speed write.

## 3. Controlled host/peer verification

- [ ] 3.1 Deploy only the experimental DLL to both clients' SmartLoader test profiles, retaining the known-good DLL for rollback. With the host's previous selection Normal, let BWAPI auto-host and record the displayed lobby speed as context, both clients' speed and measured frame pacing from the first gameplay turn, and the plugin's before/after logs. Verify that both clients actually run at index `6` and Fastest pacing; a Normal lobby alone does not fail the attempt, but index readback without pacing evidence does not pass it.
- [ ] 3.2 Repeat from an already-Fastest host selection. Verify host and peer in-game speed and measured pacing remain Fastest, the Fastest millisecond modifier stays `42`, target turn table and scheduler are `1` at match start, network latency does not change, and no unnecessary speed write occurs.
- [ ] 3.3 Correlate the host and peer frame-0 timing with pluto's opening probe in both prior-speed cases. Verify pluto logs four 4-frame samples and an accepted verdict, with no drift or observed pacing mismatch; otherwise fail validation and restore the known-good DLL.
- [ ] 3.4 Complete a full two-client auto-hosted match and a second automatically hosted match with the same experimental DLL on both sides. Verify both clients start and remain at speed index 6 with Fastest pacing and corrected timing in both games, and neither game has a drop, timeout, desync, or failed latency probe; record room text without requiring Fastest.

## 4. Decision, documentation, and rollback

- [ ] 4.1 Document results in this change's design/test evidence and update `README.md` only for behavior that actually passed. Verify the record distinguishes pre-start room speed from in-game speed and measured pacing, contains both clients' evidence and pluto verdict, and does not claim a completed validation for skipped or failed tests.
- [ ] 4.2 If either client fails to run at Fastest speed and pacing, or latency or match integrity fails, do not promote the experimental DLL: restore the working detour without the speed write and record follow-up investigation of an earlier speed-selection mechanism. Verify a retest uses the original correction and shows no experimental speed-write log. A Normal lobby alone does not trigger this fallback.
- [ ] 4.3 If all acceptance checks pass, perform and document rollback by disabling the plugin in **both** SmartLoader profiles and relaunching, then restore only the verified DLL as needed. Verify no new plugin application records appear during stock launch and its in-game speed follows normal saved-selection behavior.
