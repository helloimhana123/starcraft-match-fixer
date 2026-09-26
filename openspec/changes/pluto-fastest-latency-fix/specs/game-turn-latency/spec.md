# Spec Delta

## Purpose

Keeps the game client's per-turn action latency at the value the pluto bot was
trained for, while the match runs at the lobby game speed a human opponent
expects.

## ADDED Requirements

### Requirement: Trained action latency while playing at the target game speed

While the plugin is enabled, the client SHALL force the local game speed to
`Fastest` and present a local engine turn latency of `3` frames, so the bot
observes its trained `4`-frame action latency and keeps its latency model enabled
for the whole match.

#### Scenario: Match with the plugin active
- **WHEN** a match starts with the plugin active
- **THEN** the local game speed is `Fastest`, the engine turn latency is `3` frames, and the bot observes `4` frames without reporting a latency mismatch

#### Scenario: Another speed is selected before launch
- **WHEN** the plugin is active and the client starts with a speed other than `Fastest`
- **THEN** the plugin changes the local game speed to `Fastest` and applies the `3`-frame engine latency before the bot's latency probe

### Requirement: Re-applied for every match in a session

The fix SHALL be in effect for every match of a session, including consecutive
matches started automatically without user interaction between them.

#### Scenario: Consecutive matches
- **WHEN** a second match begins automatically after the first ends
- **THEN** the trained action latency is in effect again from the first observed turn window of that match

#### Scenario: Latency probe window
- **WHEN** the bot probes action latency at the start of a match
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

### Requirement: Predicted latency rather than modified game settings

The fix MUST reach the trained action latency by forcing local `Fastest` speed and
adjusting only local engine latency state. It MUST NOT change the network-
exchanged latency setting or unrelated match configuration.

#### Scenario: Frame pacing is untouched
- **WHEN** the fix is active during a match at the target game speed
- **THEN** the observed frame rate and the game's pacing match the local `Fastest` speed selected by the plugin

#### Scenario: No game configuration changes are required to play
- **WHEN** the operator starts a session
- **THEN** no `bwapi.ini` key, room setting, or bot configuration has to be changed to obtain the corrected latency; the required SmartLoader DLL-path registration is confined to `C:\Starcraft\mods.BWAPI.txt`

#### Scenario: SmartLoader launch loads the fix
- **WHEN** the operator writes the plugin DLL path to `C:\Starcraft\mods.BWAPI.txt` and launches `C:\Starcraft\StarCraft-SL.BWAPI.exe`
- **THEN** SmartLoader records the load in `C:\Starcraft\SmartLoader.BWAPI.log`, loads the plugin after BWAPI, and the latency fix is active in the client process

#### Scenario: Plugin is not registered
- **WHEN** the operator launches the client without the plugin DLL path in `C:\Starcraft\mods.BWAPI.txt`
- **THEN** the client retains stock latency behavior and the fix does not claim to be active

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
