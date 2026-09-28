# Proposal

## Why

BWAPI hosts matches automatically, but `speed_override` changes frame pacing rather than selecting the game's speed index. The existing validated plugin corrects the Fastest turn length but does not select Fastest if the host chose another speed; automatic matches need effective in-game Fastest pacing without losing pluto's accepted 4-frame action latency.

## What Changes

- Extend the existing 1.16.1 derivation-time detour to attempt a guarded `GameSpeed = 6` selection on each client, alongside the existing Fastest turn-table correction; record previous and resulting values and refuse on unsupported state.
- Validate the proposed detour against an intentionally Normal host lobby, with the identical plugin on host and peer. Require speed index 6, Fastest frame pacing and corrected timing on both clients from the first gameplay turn, pluto's accepted latency, and a stable match. Record the advertised lobby speed but do not require it to change to Fastest.
- Treat a runtime speed mismatch, timing drift, desync, or latency regression as a failure even if both clients loaded the plugin.
- Do not change `speed_override`, BWAPI or pluto configuration, the network-visible latency setting, or the engine executable on disk.

## Capabilities

### New Capabilities

- `game-speed-selection`: guarded, observable in-game Fastest speed on both clients during BWAPI-hosted automated matches, with acceptance gated on actual pacing, latency, and match integrity rather than lobby text.

### Modified Capabilities

None: the project has no durable specs yet. The completed `pluto-fastest-latency-fix` change contains the latency-correction delta; this change must not regress it.

## Impact

- `src/plugin.cpp` (existing detour and diagnostic log), x86 DLL build, SmartLoader host and peer profiles, and manual two-client validation.
- The current detour writes `LatencyFrames[6] = 1` after original derivation, but never writes `GameSpeed`. Existing successful logs all started at speed index `6`, so they do not establish that a late speed change can produce Fastest gameplay from a Normal starting selection.
- No new dependency or network protocol modification is intended. Rollback is disabling the plugin in both SmartLoader profiles; retain the last working DLL until validation passes.
