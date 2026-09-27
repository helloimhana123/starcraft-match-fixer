# Spec Delta

## Purpose

Makes Fastest the selected and advertised game speed when a supported client automatically hosts a Local PC lobby, so both clients inherit the same speed without relying on the host's previous UI choice or a late in-game write.

## ADDED Requirements

### Requirement: Force Fastest before room creation

On the supported StarCraft 1.16.1 host, automatically created Local PC rooms SHALL be created and advertised with game speed Fastest (index `6`) regardless of the prior or displayed selection on the map-creation screen. The selection MUST take effect before the room becomes visible to peers and MUST NOT depend on manipulating a UI control. The host SHALL NOT advertise a room as Fastest if the override did not take effect.

#### Scenario: Host previously selected Slowest or Normal
- **WHEN** BWAPI automatically hosts a Local PC game from a map-creation screen whose prior speed is Slowest or Normal
- **THEN** the new room advertises Fastest to the host and joining peer, and both clients start gameplay at Fastest speed and frame pacing

#### Scenario: Host already selected Fastest
- **WHEN** the pre-creation speed is already Fastest
- **THEN** the room remains advertised as Fastest without an unnecessary speed change

#### Scenario: Another automatic game
- **WHEN** the host creates a subsequent Local PC room after a match ends
- **THEN** that room is again advertised and played at Fastest irrespective of the retained selection

### Requirement: Preserve multiplayer agreement and corrected action latency

The change SHALL keep the host's and peer's observed room speed and in-game speed consistent, retain the accepted Fastest turn-length correction and pluto's measured four-frame action latency, and SHALL NOT alter network-visible latency or the per-speed millisecond table. Passing speed readback on one process alone does not satisfy this requirement.

#### Scenario: Full two-client game
- **WHEN** both clients join a room created under the override and play a full match
- **THEN** both observe Fastest room and gameplay pacing, pluto accepts its four-frame latency probe, and the match concludes without desync, player drop, or timeout

### Requirement: Safe refusal and observable result

Unsupported executable signatures or unrecognized creation data SHALL cause the plugin to refuse the new speed write and report why. An unsupported state MUST NOT be reported as a successfully forced Fastest room. Diagnostics SHALL distinguish the host's pre-creation value, attempted or refused override, room advertisement, and both clients' in-game observations.

#### Scenario: Unsupported creation path
- **WHEN** the executable, creation boundary, or speed data does not match the validated state
- **THEN** no pre-lobby speed write is made and a refusal is logged; no claim of a Fastest room is made

#### Scenario: Host and peer disagree
- **WHEN** the advertised speed or either client's in-game speed or pacing is not Fastest
- **THEN** the override is considered unvalidated and is not promoted for normal play
