# Proposal

## Why

BWAPI automatically hosts Local PC games without selecting a game speed. A controlled Slowest-start experiment showed that writing `GameSpeed = 6` in the existing derivation detour read back as Fastest but reverted to Slowest by the first gameplay frame. Force Fastest in the host's game-creation data *before* the map-selection OK transition creates/advertises the lobby, so the lobby and both clients inherit the chosen speed instead of relying on a late runtime correction.

## What Changes

- Determine which native StarCraft 1.16.1 creation value is consumed when the Local PC host confirms the map, and add a guarded, one-shot host-side pre-lobby override to Fastest (index `6`) at the validated creation boundary. Do not drive or depend on the UI speed control.
- Fail closed when the supported executable, creation path, or value cannot be validated; log the original value, attempted override, and resulting pre-lobby and in-game observations.
- Validate the advertised lobby speed on host and peer, in-game speed and pacing on both clients, pluto's accepted action latency, and match integrity across a full match and consecutive auto-hosted match. Retain the proven Fastest latency-table correction but do not rely on the failed derivation-time speed write.
- Keep network latency, per-speed millisecond modifiers, BWAPI/pluto configuration, and game binaries on disk unchanged.

## Capabilities

### New Capabilities

- `pre-lobby-game-speed-selection`: guaranteed Fastest selection for supported automatically hosted Local PC lobbies before room creation, independently of the UI setting, with observable host/peer outcomes and safe refusal.

### Modified Capabilities

None: `openspec/specs/` currently contains no durable capabilities. The active `force-fastest-speed-in-derivation-detour` change has an overlapping `game-speed-selection` delta whose Normal-lobby acceptance differs from this change; do not merge or archive both as independent successful speed-selection solutions without reconciling that overlap.

## Impact

- `src/plugin.cpp` (SmartLoader-loaded x86 DLL), native StarCraft 1.16.1 game-creation path, and manual host/peer testing. `bwapi-main/bwapi/BWAPI/Source/BWAPI/AutoMenuManager.cpp` is a read-only reference for the `GLUE_CREATE_MULTI` OK boundary; no BWAPI source changes are planned.
- The installed `C:\Starcraft\StarCraft.exe` and bundled `bwapi-main/Release_Binary/Starcraft/bwapi-data/data/Broodwar.map` are investigation inputs, not deployment targets. Existing detour experiments and known-good DLL remain separate for rollback; no third-party dependency is intended.
