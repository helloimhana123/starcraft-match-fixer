# Spec Delta

## Purpose

Defines the release build surface: one build directory that produces only the
injectable fix DLL, with no diagnostic executable or experimental build tree.

## ADDED Requirements

### Requirement: Single build output directory

The project SHALL build into a single output directory, `./build`, and SHALL NOT
carry a second experiment build tree.

#### Scenario: Configure and build

- **WHEN** the operator configures and builds the project in `./build`
- **THEN** all build artifacts are written under `./build` and no other build tree is present or required

#### Scenario: Previous experiment build tree

- **WHEN** the release is prepared from a checkout that contained `build-speed-experiment/`
- **THEN** that directory and its ignore rule are gone, and no documentation or build instructions reference it

### Requirement: Only the runtime artifact is produced

The build SHALL produce the injectable fast-latency fix DLL and SHALL NOT build a
separate diagnostic or inspection executable.

#### Scenario: Build outputs

- **WHEN** the project is built
- **THEN** the output contains the fix DLL with its released name and contains no `PlutoLatencyInspector` executable

#### Scenario: Inspector source is absent

- **WHEN** the release source tree is inspected
- **THEN** the inspector source file and its CMake target no longer exist, and no build instruction or README section still builds or launches an inspector

### Requirement: Release configuration

The fix DLL SHALL be built as a 32-bit Windows shared library for the validated
StarCraft 1.16.1 client.

#### Scenario: Build the release DLL

- **WHEN** the operator builds the release configuration with the documented x86 toolchain
- **THEN** the result is a 32-bit `PlutoFastestLatencyFix.dll` suitable for SmartLoader registration
