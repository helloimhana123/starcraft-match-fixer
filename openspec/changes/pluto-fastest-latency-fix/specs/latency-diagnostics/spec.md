# Spec Delta

## Purpose

Read-only log evidence about the speed and latency the loaded game client
actually uses, so the plugin's forced `Fastest`/`3` configuration can be
calibrated against what the bot measures instead of guesswork.

## ADDED Requirements

### Requirement: Read-only inspection in the plugin log

The plugin SHALL report the latency-relevant state of its host game client in
its log without modifying memory as part of diagnostics.

#### Scenario: Inspection during a match
- **WHEN** the plugin is loaded into a client that is in a match
- **THEN** its log reports the current game speed index from `0x006CDFD4`, the frame-timing table from `0x005124D8`, the per-speed turn-length table from `0x0051CE70`, and the latency setting from `0x006556E4`

#### Scenario: Plugin is not loaded
- **WHEN** no supported game client loads the plugin
- **THEN** no diagnostic claim is emitted and the absence of the plugin log indicates that inspection did not occur

#### Scenario: Client state is inaccessible
- **WHEN** the plugin cannot safely read the host client's validated memory state
- **THEN** its log reports the access or validation problem and records that no changes were attempted

### Requirement: Reconciliation with the latency the bot measured

The plugin log SHALL report the bot's own measured action latency next to the
client's engine values, so the relationship between the engine's turn length
and the latency the bot observes can be confirmed from evidence.

#### Scenario: Report correlates both sources
- **WHEN** the plugin records a match for which the bot recorded its measured latency
- **THEN** its log shows both the engine's turn length for the selected game speed and the latency value the bot recorded

#### Scenario: Bot measurement is missing
- **WHEN** no bot measurement is available for the selected session
- **THEN** the plugin log reports that the measurement is unavailable instead of inferring one

### Requirement: Calibration statement for the forced target speed

The plugin log SHALL state, for a selected game speed, the engine latency value that
must be in effect for the bot's trained action latency to hold, so the fix's
target value is derived from evidence rather than assumed.

#### Scenario: Target value is stated
- **WHEN** the plugin has a known bot measurement
- **THEN** its log states that the required local target is `Fastest` with engine latency `3`, producing the trained `4`-frame action latency, and whether the client currently satisfies it

#### Scenario: Evidence is insufficient
- **WHEN** the required evidence is not available
- **THEN** the plugin log reports what is missing rather than emitting a target value

### Requirement: Validated client identification

The tool SHALL identify whether the client it inspected is a build it has been
validated against, and SHALL report that verdict rather than assuming
compatibility.

#### Scenario: Supported build
- **WHEN** the plugin is loaded into a validated build
- **THEN** its log says the build is supported and identifies the validated 1.16.1 address layout

#### Scenario: Unsupported build
- **WHEN** the plugin is loaded into a client that is not a validated build
- **THEN** its log says the build is unsupported and records that no write was attempted
