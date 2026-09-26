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
  process. The plugin uses the host process's own memory, forces the game speed
  to `Fastest`, and holds the engine's turn-length table at `3` frames so pluto
  observes its trained `4`-frame action latency.
- Load that plugin through SmartLoader by writing its DLL path to
  `C:\Starcraft\mods.BWAPI.txt` and launching
  `C:\Starcraft\StarCraft-SL.BWAPI.exe`. SmartLoader records its load activity
  in `C:\Starcraft\SmartLoader.BWAPI.log`.
- Add log-based diagnostic output from the in-process plugin that reports the
  engine's live speed/latency state and the latency value pluto actually
  measured, so the target can be calibrated and drift detected without a
  separate inspector executable.
- **BREAKING** (play workflow): the client must be launched through
  `StarCraft-SL.BWAPI.exe` with the plugin registered in `mods.BWAPI.txt`.
- No change to pluto, BWAPI, `bwapi.ini` semantics, room/map configuration, or
  the network-exchanged latency setting. The plugin intentionally forces the
  local game speed to `Fastest` when enabled.

## Capabilities

### New Capabilities
- `game-turn-latency`: the client's game speed is forced to `Fastest` and its
  local per-turn latency is held at `3` frames so pluto observes `4` frames,
  re-applied every match without altering the network-exchanged latency setting.
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
  the network latency setting at `0x006556E4`. Both 1.16.1.1 clients were
  verified byte-identical at the relevant code sites, and the image has
  `RELOCS_STRIPPED`, so no ASLR relocation is needed.
- Game install: one SmartLoader DLL-path registration in
  `C:\Starcraft\mods.BWAPI.txt`, plus the SmartLoader log at
  `C:\Starcraft\SmartLoader.BWAPI.log` and the plugin log.
- Unchanged: `pluto.dll`, `BWAPI.dll`, `bwapi.ini`, pluto's own configuration,
  room settings, network protocol.
- Constraints: 32-bit build only; validation is a manual two-client LAN match
  against pluto, since there is no automated harness for the game.
