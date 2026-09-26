# Spec Delta

## Purpose

Read-only evidence about the speed and latency a game client actually uses, so
the plugin's forced `Fastest`/`3` configuration can be calibrated against what
the bot measures instead of guesswork.

## ADDED Requirements

### Requirement: Read-only inspection of a running client

The diagnostic tool SHALL report the latency-relevant state of a chosen running
game client without modifying that process.

#### Scenario: Inspection during a match
- **WHEN** the tool is pointed at a client that is in a match
- **THEN** it reports the current game speed index from `0x006CDFD4`, the frame-timing table from `0x005124D8`, the per-speed turn-length table from `0x0051CE70`, and the latency setting from `0x006556E4`

#### Scenario: No matching client
- **WHEN** the tool is run while no game client is running
- **THEN** it reports that no client was found and exits with a failure status

#### Scenario: Client state is inaccessible
- **WHEN** the client process cannot be accessed by the tool
- **THEN** it reports the access problem, suggests the cause it can determine, and exits with a failure status without attempting changes

### Requirement: Reconciliation with the latency the bot measured

The tool SHALL report the bot's own measured action latency next to the client's
engine values, so the relationship between the engine's turn length and the
latency the bot observes can be confirmed from evidence.

#### Scenario: Report correlates both sources
- **WHEN** the tool is run after a match for which the bot recorded its measured latency
- **THEN** the report shows both the engine's turn length for the selected game speed and the latency value the bot recorded

#### Scenario: Bot measurement is missing
- **WHEN** no bot measurement is available for the selected session
- **THEN** the tool reports that the measurement is unavailable instead of inferring one

### Requirement: Calibration statement for the forced target speed

The tool SHALL state, for a selected game speed, the engine latency value that
must be in effect for the bot's trained action latency to hold, so the fix's
target value is derived from evidence rather than assumed.

#### Scenario: Target value is stated
- **WHEN** the tool is run against a client with a known bot measurement
- **THEN** it states that the required local target is `Fastest` with engine latency `3`, producing the trained `4`-frame action latency, and whether the client currently satisfies it

#### Scenario: Evidence is insufficient
- **WHEN** the required evidence is not available
- **THEN** the tool reports what is missing rather than emitting a target value

### Requirement: Validated client identification

The tool SHALL identify whether the client it inspected is a build it has been
validated against, and SHALL report that verdict rather than assuming
compatibility.

#### Scenario: Supported build
- **WHEN** the inspected client is a validated build
- **THEN** the report says the build is supported and identifies the validated 1.16.1 address layout

#### Scenario: Unsupported build
- **WHEN** the inspected client is not a validated build
- **THEN** the report says the build is unsupported and exits with a failure status
