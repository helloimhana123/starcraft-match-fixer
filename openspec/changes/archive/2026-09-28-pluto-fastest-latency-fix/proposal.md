# Proposal

## Why

The pluto (BWRL) bot measures its action latency at the start of every match and
requires it to be 4 frames. At the `Fastest` lobby speed the StarCraft 1.16.1
engine derives a longer turn (we measure 6), so pluto prints "action latency is
6 frames here, trained for 4 (lobby turn rate / latency setting) - playing on,
micro will be off" and disables its latency model for the whole game. `Fastest`
is the speed used for human play, so the bot currently cannot be played against
at full strength, and every settings-level lever has been falsified: the engine
*derives* its per-speed turn length from the millisecond speed table, so latency
cannot be set independently of game speed. `/speed`, `speed_override` and
`LatencyChanger` each either change nothing observable or desynchronise the two
clients (observed: the pluto instance dies).

## What Changes

- Add a 32-bit native plugin that SmartLoader loads into the StarCraft client
  process. It begins in a read-only calibration mode that records the host's
  live engine state beside pluto's measured action latency. It MUST NOT enable
  a latency write target until that measurement relationship is established.
- Load the plugin through SmartLoader profiles: the bot uses
  `C:\Starcraft\mods.pluto.txt` with `C:\Starcraft\StarCraft-SL.pluto.exe`,
  while the peer uses `C:\Starcraft\mods.txt` with
  `C:\Starcraft\StarCraft-SL.exe`. SmartLoader records the loads in
  `SmartLoader.pluto.log` and `SmartLoader.log` respectively.
- Add log-based diagnostic output from the in-process plugin that reports the
  engine's live speed/latency state and the latency value pluto actually
  measured, so the target can be calibrated and drift detected without a
  separate inspector executable.
- **BREAKING** (play workflow): both test clients must use their SmartLoader
  profile launcher with the plugin registered in the corresponding profile file.
- No change to pluto, BWAPI, `bwapi.ini` semantics, room/map configuration, or
  the network-exchanged latency setting. The plugin intentionally forces the
  local game speed to `Fastest` when enabled.

## Capabilities

### New Capabilities
- `game-turn-latency`: after calibration validates a concrete relationship, the
  client can apply the measured local engine correction on every match without
  altering the network-exchanged latency setting. The previously proposed
  `Fastest`/`3` target is superseded by a read-only investigation of the
  initialization path. Direct live scheduler writes produced local 4-frame
  samples but proved session-unsafe and are not a delivery mechanism.
- `latency-diagnostics`: read-only observation of the engine's speed and latency
  tables plus the bot's measured action latency, used to calibrate the target
  and to detect drift or an unsupported client build.

### Modified Capabilities
<!-- None: this project has no existing specs. -->

## Impact

- New code: a 32-bit native plugin plus build scripts (MSVC x86 + CMake).
  The plugin log is the inspection and calibration output; no separate
  inspector executable or external reader process is required.
- Runtime reach: the injected DLL accesses the host StarCraft process's memory
  only. Validated 1.16.1 addresses are `GameSpeed` at `0x006CDFD4`,
  `GameSpeedModifiers` at `0x005124D8`, `LatencyFrames` at `0x0051CE70`, and
  the network latency setting at `0x006556E4`. A write-disabled Fastest run
  recorded speed `6`, a latency-frame value of `2`, and pluto's `6`-frame
  verdict; this disproves the previously assumed direct mapping and requires
  further calibration before writes. Both 1.16.1.1 clients were verified
  byte-identical at the relevant code sites, and the image has
  `RELOCS_STRIPPED`, so no ASLR relocation is needed.
- Game install: one SmartLoader DLL-path registration in
  `C:\Starcraft\mods.BWAPI.txt`, plus the SmartLoader log at
  `C:\Starcraft\SmartLoader.BWAPI.log` and the plugin log.
- Unchanged: `pluto.dll`, `BWAPI.dll`, `bwapi.ini`, pluto's own configuration,
  room settings, network protocol.
- Constraints: 32-bit build only; validation is a manual two-client LAN match
  against pluto, since there is no automated harness for the game.
