# Design

> **Superseded (2026-09-27).** `force-fastest-speed-before-lobby` identified the
> host's pre-advertisement creation-speed byte (`+0x26`, written by `0x004A68D0`)
> and forces it to Fastest before room creation. A Slowest host then advertised
> Fastest to both clients and both played at `speed=6` with the corrected turn
> table. This change's approach (a late `GameSpeed` write after the derivation
> detour, accepting a Normal lobby) is superseded and must not be promoted as an
> independent solution. Keep it only as history and reconcile before syncing.

## Context

See `proposal.md` for motivation and `specs/game-speed-selection/spec.md` for the acceptance contract. At commit `ecac8e7`, `src/plugin.cpp` validates the six-byte signature at `0x004D92A0`, installs a process-lifetime detour, calls the original derivation and sets only `LatencyFrames[6] = 1`. It reads `GameSpeed` (`0x006CDFD4`) for diagnostics but never writes it. Successful logs show Fastest already selected, corrected table/scheduler values of 1 at frame 0 and pluto's 4-frame verdict. A subsequent lobby-only test showed Normal on both clients, but did not enter gameplay. Both host and peer can load the same plugin; whether the detour produces Fastest in-game behavior from that Normal start remains untested.

## Goals / Non-Goals

**Goals:**

- Try to add a validated, bounded `GameSpeed = 6` write to the existing derivation callback without changing the successful timing correction.
- Gate acceptance on both clients' actual in-game speed and frame pacing, bot probe, and full-match integrity rather than on in-process speed readback alone.
- Log enough before/after and per-match evidence to detect an ineffective or unsafe attempt.

**Non-Goals:**

- Change or claim to change the lobby dropdown before room creation; a Normal lobby is acceptable if gameplay on both clients passes every in-game check.
- Change `GameSpeedModifiers`, the network latency setting (`0x006556E4`), the scheduler field (`0x0051CEA0`), BWAPI configuration, or on-disk game binaries.

## Decisions

### D1: Make the existing detour the controlled experiment, not an assumed lobby fix

After the trampoline executes, read the current speed with guarded access. Accept only a supported index (`0..6`) and an expected engine signature; write `6` only if needed, then retain the existing Fastest table correction. Verify and log the speed before/after and result. A speed already equal to 6 is a no-op. Preserve the existing entry-signature gate and avoid a late watcher-thread speed write, which could race the active scheduler. Do not imply that the derivation entry signature alone proves the entire image or in-game pacing; validate observed state before a new speed write. In a match started from a Normal lobby, require both clients to run with Fastest pacing and corrected timing from the first gameplay turn; reading `6` alone is insufficient.

Alternative: alter `speed_override` or the speed-modifier table. Rejected because these modify pacing globally and risk breaking the calibrated correction. A BWAPI auto-menu change could select Fastest in the lobby, but is unnecessary for the requested outcome if the detour achieves stable Fastest gameplay on both clients.

### D2: Prove gameplay pacing, not just process-local speed

Record the hosted room's speed as context, and collect timestamped host/peer logs of derivation calls, observed speed and timing at frame 0. With the previous host selection deliberately Normal, compare both clients' speed index, observed frame pacing, selected turn length and drift during gameplay. Identical runtime writes on both clients do not by themselves prove synchronized behavior. Capture unchanged network latency and Fastest millisecond-table entry (42) separately from the selected turn length (1). Verify pluto's measured four-frame action latency rather than inferring it from the table. Do not claim the room advertises Fastest if it continues to display Normal.

Alternative: accept host and peer `GameSpeed == 6` as proof. Rejected because it cannot prove actual pacing, player agreement or stable network behavior.

### D2a: Experimental result and post-derivation speed investigation

With a Slowest (index `0`) host selection and the experimental DLL on both clients, per-invocation logs show the same sequence on host and peer: invocation 1 read `GameSpeed = 0`, wrote `6`, and read back `6`; invocation 2 read `6` and made no speed write. At the first initialized gameplay observation (frame 0 on the host, frame 1 on the peer), both read `GameSpeed = 0`. Both still reported the Fastest turn-table entry `1` and scheduler turn length `1`. A subsequent run on a fixed map repeated the `0 -> 6 -> 0` speed sequence. Pluto recorded four 4-frame samples in that run, but the speed requirement failed: this does not demonstrate Fastest gameplay. The server's earlier termination was not tied to a new crash report; a fixed-map run progressed further, so its cause is not established here.

Read-only disassembly of the installed `C:\Starcraft\StarCraft.exe` identifies several *possible* writers of `GameSpeed` at `0x006CDFD4`:

- `0x004DEB98` writes the speed passed to a timing-setup routine, which then calls the derivation function at `0x004DECB5`. This explains how a speed write can precede a detour invocation, not the observed later restoration by itself.
- The game-start routine beginning at `0x004D9950` contains conditional speed stores at `0x004D9A0D` and `0x004D9A20`.
- At `0x004C979E`, a routine copies nine 32-bit words starting at `GameSpeed` from another structure; depending on the runtime path, that copy could restore a saved speed. Its adjacent replay-related branch also needs runtime interpretation.
- `0x004A69E5` and `0x004868D4` are additional direct stores; their presence alone does not establish that either ran between the detour and the frame-zero sample.

These static sites do **not** identify which instruction wrote `0` in the tested match, or prove whether the speed was restored once or multiple times. The next read-only investigation is to capture a write watchpoint on `0x006CDFD4` during a controlled Slowest-start match, recording the instruction pointer, call stack, old/new values and order relative to both detour invocations and the first gameplay frame. Do not select a replacement write point or treat the present detour experiment as effective until that runtime writer is identified and the in-game behavior can be validated on both clients.

### D3: Gate promotion and preserve rollback

Keep the known-good DLL available and test a new build on both clients in a controlled environment; do not replace normal-play deployment based only on a successful local readback or bot probe. Require a full match and consecutive auto-hosted match with both clients using the same build. A Normal lobby alone is not a failure; a non-Fastest in-game speed/pacing, pluto latency rejection, or integrity failure is. Disable both SmartLoader profile entries for stock rollback if a crash or desync occurs. Do not claim rollback is verified until tested.

## Risks / Trade-offs

- **Derivation executes after room advertisement** -> record the lobby speed as context; accept only if both clients demonstrably run at Fastest without a gameplay mismatch, and do not claim the lobby changed.
- **Speed changes after timing initialization or after the engine restores a negotiated value** -> log first initialized state and subsequent changes on both clients; abort promotion on drift or inconsistent pacing.
- **Two peers with identical memory still disagree with negotiated room state** -> require matching observed in-game pacing, no desync, and a full natural match.
- **Unsupported build or speed value** -> refuse the new speed write with a clear log, retain validation gates for the existing detour; do not interpret partial success as passing.
- **Existing `PLUTO_FASTEST_LATENCY_DRY_RUN` reader is not applied to detour installation** -> do not rely on that environment variable as a rollback; keep a known-good DLL and removable SmartLoader entries.

## Migration Plan

1. Preserve the working binary and baseline logs; build a separate experimental x86 DLL.
2. Test an already-Fastest baseline, then a deliberately Normal prior host selection; record the lobby text without making it an acceptance gate. Install the same experimental DLL in both SmartLoader profiles only for controlled tests.
3. Validate both clients' in-game speed and pacing, pluto verdict, full match, second auto-hosted match and actual rollback. Promote only if all checks pass; otherwise return to the last working DLL and investigate an earlier game-speed selection mechanism separately.
