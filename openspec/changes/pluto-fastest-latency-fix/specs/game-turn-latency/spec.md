# Spec Delta

## Purpose

Keeps the game client's per-turn action latency at the value the pluto bot was
trained for, while the match runs at the lobby game speed a human opponent
expects.

## ADDED Requirements

### Requirement: Evidence-gated latency correction while playing at the target game speed

The plugin SHALL remain read-only until calibration establishes which validated
local engine state determines pluto's measured action latency. It SHALL NOT
force `Fastest` or write a fixed turn-latency value merely from the previously
assumed `Fastest`/`3` mapping.

The plugin SHALL be a 32-bit DLL operating on the loaded StarCraft executable's
own memory. Until a pre-initialization correction mechanism is validated, it
SHALL remain read-only after detecting initialized timing state. It SHALL use
the validated 1.16.1 addresses `GameSpeed = 0x006CDFD4` and `LatencyFrames =
0x0051CE70`, and SHALL NOT modify the network latency setting at `0x006556E4`.

#### Scenario: Calibration is incomplete
- **WHEN** a match starts before a correction has been validated
- **THEN** the plugin records diagnostic evidence and performs no game-speed or latency-frame writes

#### Scenario: Live scheduler write is rejected
- **WHEN** the plugin has only the currently observed table and scheduler evidence
- **THEN** it does not write the live scheduler field and records that a pre-initialization mechanism must be validated before correction can be enabled

#### Scenario: Engine timing state is not initialized
- **WHEN** the plugin observes a zero or inaccessible selected latency-frame or scheduler turn-length value
- **THEN** it performs no timing or game-speed write and records timestamped waiting and timing-state-ready events for the pre-initialization investigation

### Requirement: Re-applied for every match in a session after validation

After a correction has been validated, the fix SHALL be in effect for every
match of a session, including consecutive matches started automatically without
user interaction between them.

#### Scenario: Consecutive matches after calibration
- **WHEN** a second match begins automatically after the first ends with a validated correction enabled
- **THEN** the trained action latency is in effect again from the first observed turn window of that match

#### Scenario: Latency probe window after calibration
- **WHEN** the bot probes action latency at the start of a match with a validated correction enabled
- **THEN** the corrected value is already in effect before the probe completes

### Requirement: Match integrity with a peer client

A peer client SHALL be able to join and play a full match against the bot with
the fix active, without a desync, drop, timeout, or any difference in the room
settings and frame pacing compared to a stock match at that game speed.

#### Scenario: Peer joins and completes a match
- **WHEN** a peer client joins a match hosted by the bot with the fix active
- **THEN** the match runs to completion with no disconnect or "player not responding" outcome

#### Scenario: Room settings are unchanged
- **WHEN** the peer inspects the room before and during the match
- **THEN** the game speed and latency settings are the same values it would see in a stock match at that game speed

### Requirement: Calibrated latency rather than protocol-visible game settings

The fix MUST reach the trained action latency only through a calibration-validated
local engine correction. It MUST NOT change the network-exchanged latency setting
or unrelated match configuration.

#### Scenario: Frame pacing is observed during calibration
- **WHEN** calibration runs at a selected lobby speed
- **THEN** the plugin records the associated engine state and pluto verdict without modifying frame pacing

#### Scenario: No game configuration changes are required to play
- **WHEN** the operator starts a session
- **THEN** no `bwapi.ini` key, room setting, or bot configuration has to be changed to obtain the corrected latency; the required SmartLoader DLL-path registration is confined to `C:\Starcraft\mods.BWAPI.txt`

#### Scenario: SmartLoader launch loads the fix
- **WHEN** the operator writes the plugin DLL path to `C:\Starcraft\mods.BWAPI.txt` and launches `C:\Starcraft\StarCraft-SL.BWAPI.exe`
- **THEN** SmartLoader records the load in `C:\Starcraft\SmartLoader.BWAPI.log`, loads the plugin after BWAPI, and the latency fix is active in the client process

#### Scenario: Plugin is not registered
- **WHEN** the operator launches the client without the plugin DLL path in `C:\Starcraft\mods.BWAPI.txt`
- **THEN** the client retains stock latency behavior and the fix does not claim to be active

#### Scenario: Unsupported memory signature
- **WHEN** the DLL is loaded by any launcher executable but the validated speed table is not present
- **THEN** it performs no StarCraft memory writes and records the refusal

### Requirement: Safe behaviour on unsupported or unexpected client state

The fix MUST confine itself to client builds and engine states it has been
validated against. When the client build or the observed engine values are not
recognised, it MUST leave the process unmodified and record the refusal.

#### Scenario: Unrecognised client build
- **WHEN** the fix runs inside a client build it was not validated against
- **THEN** it makes no modification and logs that the build is unsupported

#### Scenario: Unexpected engine value
- **WHEN** the observed latency value for the target game speed is not a value the fix expects
- **THEN** it makes no modification and logs the observed value

### Requirement: Observable application record

The fix SHALL produce an observable record, per match, of the game speed index,
the latency value before and after its work, and whether it applied or refused
to apply a change.

#### Scenario: Record of an applied change
- **WHEN** the fix applies a change in a match
- **THEN** its record contains the game speed, the previous value, the applied value, and the fact that it applied

#### Scenario: Record of a refused change
- **WHEN** the fix refuses to apply a change
- **THEN** its record states the reason and the value it observed
