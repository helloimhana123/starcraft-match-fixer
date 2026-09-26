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

- Add a 32-bit native DLL that is loaded into the StarCraft client process
  **after** BWAPI and holds the engine's per-speed turn-length table at the
  value pluto requires, while the lobby game speed stays `Fastest`.
- Load that DLL without a runtime injector by generating client executables with
  an added bootstrap PE section that calls `LoadLibraryA` - the same technique
  BWAPI's own `loader` uses today.
- Add a read-only diagnostic mode that reports the engine's live speed/latency
  state and the latency value pluto actually measured, so the target can be
  calibrated and drift detected.
- **BREAKING** (play workflow): the client must be launched from the generated
  executable rather than the plain one, so the DLL is present in the process.
- No change to pluto, BWAPI, `bwapi.ini` semantics, room/map settings, or
  anything exchanged over the network.

## Capabilities

### New Capabilities
- `game-turn-latency`: the client's per-turn latency is set and held at the
  value pluto requires while the lobby game speed is `Fastest`, re-applied every
  match, without altering frame pacing or networked game settings.
- `latency-diagnostics`: read-only observation of the engine's speed and latency
  tables plus the bot's measured action latency, used to calibrate the target
  and to detect drift or an unsupported client build.

### Modified Capabilities
<!-- None: this project has no existing specs. -->

## Impact

- New code: a 32-bit DLL plus a bootstrap generator and build scripts
  (MSVC x86 + CMake; LIEF for PE editing, available in the sibling
  `starcraft-bwapi` build tree).
- Runtime reach: two processes, in memory only - the engine's latency table at a
  fixed address (`0x0051CE70` + 4 x speed index). Both 1.16.1.1 clients were
  verified byte-identical at the relevant code sites, and the image has
  `RELOCS_STRIPPED`, so no ASLR relocation is needed.
- Game install: generated executable variants alongside `Starcraft-BWAPI.exe`
  and `StarCraft.exe`, plus a log file.
- Unchanged: `pluto.dll`, `BWAPI.dll`, `bwapi.ini`, pluto's own configuration,
  room settings, network protocol.
- Constraints: 32-bit build only; validation is a manual two-client LAN match
  against pluto, since there is no automated harness for the game.
