# Spec Delta

## Purpose

Ensures both clients in automatically hosted StarCraft matches actually run at Fastest in-game speed and pacing while preserving the bot's calibrated action latency.

## ADDED Requirements

### Requirement: Effective Fastest selection for automated hosting

When BWAPI automatically hosts a match with the supported plugin enabled on both clients, both clients SHALL use Game Speed index 6 with Fastest frame pacing from the first gameplay turn. The advertised lobby speed MAY retain the host's prior selection; lobby text alone SHALL NOT determine whether the in-game speed correction succeeded.

#### Scenario: Host begins with a different stored speed
- **WHEN** BWAPI auto-hosts after the host's previous speed selection was Normal
- **THEN** host and peer each observe speed index 6 and Fastest pacing from the first gameplay turn, even if the pre-match lobby still shows Normal

#### Scenario: Host already selected Fastest
- **WHEN** BWAPI auto-hosts with Fastest already selected
- **THEN** both clients remain at in-game Fastest speed and pacing without a timing regression

#### Scenario: Multiple automatic matches
- **WHEN** BWAPI automatically starts another match after the first one ends
- **THEN** both clients again use in-game Fastest speed and pacing from the first gameplay turn of the second match

### Requirement: Preserve validated action latency and match integrity

The speed selection SHALL preserve the existing corrected Fastest turn length and pluto's accepted four-frame action latency. It MUST NOT alter the network-visible latency setting or produce a host/peer in-game speed or pacing mismatch, disconnect, timeout, or desync.

#### Scenario: Opening probe with both clients enabled
- **WHEN** a match starts from a non-Fastest prior speed with the identical plugin enabled for host and peer
- **THEN** both clients observe the corrected Fastest timing from match start and pluto reports accepted four-frame action latency

#### Scenario: Full two-client match
- **WHEN** host and peer play a full automatically hosted match after Fastest selection
- **THEN** it reaches a natural conclusion without desync, player drop, or timeout, and the exchanged latency setting is unchanged

### Requirement: Safe refusal and observable results

The plugin MUST refuse speed writes on unrecognized code or engine state. It SHALL log the observed prior speed, attempted or refused selection, resulting speed and timing, and enough per-match information to correlate its result with both clients' gameplay and pluto's verdict. A runtime correction MUST NOT be described as having changed the advertised lobby speed without separate evidence.

#### Scenario: Unexpected client state
- **WHEN** the supported entry signature or speed-state validation fails
- **THEN** no new speed write is performed and the reason is recorded

#### Scenario: Lobby retains Normal but gameplay is Fastest
- **WHEN** the host room advertises Normal but both clients start gameplay at speed index 6 with Fastest pacing and pass the latency and integrity checks
- **THEN** the in-game correction is accepted without claiming that the lobby speed changed

#### Scenario: Plugin absent on either client
- **WHEN** either client launches without the plugin
- **THEN** no claim of a validated symmetric in-game Fastest correction is made and stock behavior remains available by removing plugin registration
