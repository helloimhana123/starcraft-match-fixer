# Spec Delta

## Purpose

Defines the release behavior of the injected plugin: a quiet, unconfigurable
runtime that keeps the validated Fastest fix and reports only failures.

## ADDED Requirements

### Requirement: Error-only logging

During normal operation the plugin SHALL NOT write any log line. The plugin
SHALL write a log line only for a failure: a refusal to act, a failed hook or
detour installation, or a fatal startup error.

#### Scenario: Successful run

- **WHEN** a supported client loads the plugin and the fix applies normally
- **THEN** the plugin produces no log output for that session

#### Scenario: Refused or failed action

- **WHEN** the plugin refuses an action because a signature, address, or engine value cannot be validated, or a hook or detour cannot be installed
- **THEN** it writes a single log line stating the failure and its reason

#### Scenario: Fatal startup error

- **WHEN** the plugin cannot start its worker (for example, the worker thread cannot be created)
- **THEN** it writes a log line identifying the fatal error

### Requirement: No environment-variable configuration

The plugin SHALL NOT read environment variables to change its behavior. Its
validated behavior SHALL be active by default with no runtime configuration.

#### Scenario: Diagnostic environment variables set

- **WHEN** any previously supported diagnostic environment variable is present in the client environment
- **THEN** the plugin's behavior is unchanged and it performs no diagnostic tracing, dry run, log-path selection, or bot-measurement reporting

#### Scenario: Default behavior

- **WHEN** a supported client loads the plugin with a default environment
- **THEN** the pre-lobby Fastest override and the Fastest turn-length correction are both active

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

The plugin SHALL retain the validated pre-lobby Fastest override and the
derivation-time Fastest turn-length correction, each gated by exact signature
validation, and SHALL NOT modify network-visible latency, the per-speed
millisecond table, or the game binaries on disk.

#### Scenario: Applying the fix

- **WHEN** a supported host creates a room and the engine derivation runs
- **THEN** the creation speed is forced to Fastest and the Fastest turn length is corrected, with no change to the network latency setting or the per-speed millisecond table

#### Scenario: Safety gates preserved

- **WHEN** the validated creation or derivation signature does not match the running client
- **THEN** the plugin refuses the corresponding write and logs the refusal instead of writing blindly
