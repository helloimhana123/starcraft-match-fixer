# Spec Delta

## Purpose

Read-only log evidence about the speed and latency the loaded game client
actually uses, so a safe pre-initialization mechanism can be identified from
what the bot measures instead of guesswork.

## ADDED Requirements

### Requirement: Read-only inspection in the plugin log

The plugin SHALL report the latency-relevant state of its host game client in
its log without modifying memory as part of diagnostics.

#### Scenario: Inspection during a match
- **WHEN** the plugin is loaded into a client that is in a match
- **THEN** its log reports the current game speed index from `0x006CDFD4`, the frame-timing table from `0x005124D8`, the per-speed turn-length table from `0x0051CE70`, live scheduler length from `0x0051CEA0`, the latency setting from `0x006556E4`, and timestamped initialization-boundary events

#### Scenario: Plugin is not loaded
- **WHEN** no supported game client loads the plugin
- **THEN** no diagnostic claim is emitted and the absence of the plugin log indicates that inspection did not occur

#### Scenario: Client state is inaccessible
- **WHEN** the plugin cannot safely read the host client's validated memory state
- **THEN** its log reports the access or validation problem and records that no changes were attempted

### Requirement: Reconciliation with the latency the bot measured

The plugin log SHALL report the bot's own measured action latency next to the
client's engine values as independent observations, so the relationship between
the engine state and the latency the bot observes can be established from
evidence rather than inferred from one table entry.

#### Scenario: Report correlates both sources
- **WHEN** the plugin records a match for which the bot recorded its measured latency
- **THEN** its log shows the complete observed engine state for the selected speed and the latency value the bot recorded

#### Scenario: Bot measurement is missing
- **WHEN** no bot measurement is available for the selected session
- **THEN** the plugin log reports that the measurement is unavailable instead of inferring one

### Requirement: Calibration verdict for a proposed target speed

The plugin log SHALL state whether available evidence validates a proposed
engine correction for a selected game speed. It SHALL identify any observed
contradiction instead of declaring a target derived from an unverified mapping.

#### Scenario: Target value is validated
- **WHEN** repeated calibration evidence establishes a correction that produces the trained action latency
- **THEN** its log states the validated target of `Fastest` with scheduler and selected latency-frame value `1`, whether both values were initialized before application, and whether matching read-backs were observed

#### Scenario: Evidence is insufficient
- **WHEN** the required evidence is not available
- **THEN** the plugin log reports what is missing rather than emitting a target value

#### Scenario: Proposed mapping contradicts live evidence
- **WHEN** the bot's measured latency does not match the value predicted from the observed engine state
- **THEN** the plugin log reports the observations and marks the proposed mapping unvalidated without enabling a write target

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
