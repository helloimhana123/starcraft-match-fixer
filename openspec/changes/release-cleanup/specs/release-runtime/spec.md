# Spec Delta

## Purpose

Defines the release behavior of the injected plugin: a quiet runtime that reads
its two settings from `MatchFixer.ini` with no built-in defaults, keeps the
validated fix, and reports only failures.

## ADDED Requirements

### Requirement: Error-only logging

During normal operation the plugin SHALL NOT write any log line. The plugin
SHALL write a log line only for a failure: a refusal to act, a failed hook or
detour installation, missing or invalid configuration, or a fatal startup error.

#### Scenario: Successful run

- **WHEN** a supported client loads the plugin with a valid `MatchFixer.ini` and the fix applies normally
- **THEN** the plugin produces no log output for that session

#### Scenario: Refused or failed action

- **WHEN** the plugin refuses an action because a signature, address, or engine value cannot be validated, or a hook or detour cannot be installed
- **THEN** it writes a single log line stating the failure and its reason

#### Scenario: Missing or invalid configuration

- **WHEN** `MatchFixer.ini` is absent or a configured value is absent, unparseable, or out of range
- **THEN** the plugin writes a log line stating the configuration refusal for the affected step

#### Scenario: Fatal startup error

- **WHEN** the plugin cannot start its worker (for example, the worker thread cannot be created)
- **THEN** it writes a log line identifying the fatal error

### Requirement: No environment-variable configuration

The plugin SHALL NOT read environment variables to change its behavior. Its only
configuration input SHALL be `MatchFixer.ini`.

#### Scenario: Diagnostic environment variables set

- **WHEN** any previously supported diagnostic environment variable is present in the client environment
- **THEN** the plugin's behavior is unchanged and it performs no diagnostic tracing, dry run, log-path selection, or bot-measurement reporting

#### Scenario: Configuration source

- **WHEN** the plugin determines which speed and latency values to apply
- **THEN** it uses only `MatchFixer.ini`; no environment variable changes its behavior

### Requirement: Fix configuration from MatchFixer.ini

The plugin SHALL read its behavior configuration from `MatchFixer.ini` in the
client working directory. It SHALL ignore lines beginning with `;`. It SHALL
recognize two integer keys: `GameSpeed`, a value in `0..6`, and `LatencyFrames`,
a value in `1..20`. The plugin SHALL NOT supply built-in defaults: a fix step
applies only when its key is present and valid.

#### Scenario: Valid configuration

- **WHEN** `MatchFixer.ini` supplies `GameSpeed` in `0..6` and `LatencyFrames` in `1..20`
- **THEN** the pre-lobby override forces the configured speed into the creation data and the derivation correction writes the configured latency-frame value to every entry of the per-speed latency-frame table

#### Scenario: Comment lines

- **WHEN** `MatchFixer.ini` contains lines beginning with `;`
- **THEN** those lines are ignored and the configured values still apply

#### Scenario: Missing file

- **WHEN** `MatchFixer.ini` is absent from the client working directory
- **THEN** neither the speed override nor the latency correction performs a write, and the plugin logs that no configuration was found

#### Scenario: Invalid or out-of-range value

- **WHEN** a key is absent, unparseable, or outside its range
- **THEN** the step that depends on that key performs no write and the plugin logs the reason

#### Scenario: Independent steps

- **WHEN** only one of the two keys is present and valid
- **THEN** only the corresponding step applies, and the other step performs no write and is logged as refused

### Requirement: No live-state inspection

The plugin SHALL NOT install hooks or run observation work whose only purpose is
to inspect or trace engine state, and SHALL NOT emit routine observations of the
live engine state.

#### Scenario: Normal match

- **WHEN** a match runs with the plugin loaded
- **THEN** no state-observation, creation-trace, calibration, or readiness output is emitted, and only hooks needed to apply the validated fix are installed

#### Scenario: Unsupported client state

- **WHEN** the supported code signature or engine state is not recognized
- **THEN** the plugin performs no write and records the refusal as a failure

### Requirement: Retained validated fix

The plugin SHALL retain the validated pre-lobby creation-speed override and the
derivation-time latency-frame correction, each gated by exact signature
validation, and SHALL NOT modify network-visible latency, the per-speed
millisecond table, or the game binaries on disk.

#### Scenario: Applying the fix

- **WHEN** a supported host creates a room, `MatchFixer.ini` supplies valid values, and the engine derivation runs
- **THEN** the creation speed is forced to the configured `GameSpeed` and every latency-frame entry is set to the configured `LatencyFrames`, with no change to the network latency setting or the per-speed millisecond table

#### Scenario: Safety gates preserved

- **WHEN** the validated creation or derivation signature does not match the running client
- **THEN** the plugin refuses the corresponding write and logs the refusal instead of writing blindly
