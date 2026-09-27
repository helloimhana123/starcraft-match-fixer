# Design

## Context

See `proposal.md` for motivation and `specs/pre-lobby-game-speed-selection/spec.md` for the behavioral contract. The current x86 SmartLoader plugin in `src/plugin.cpp` already installs a signature-gated detour around the engine's turn-length derivation at `0x004D92A0` and corrects `LatencyFrames[6]` to `1`. Its experimental speed write at `0x006CDFD4` happens too late: a Slowest-start test logged `0 -> 6` at derivation and `0` again at first gameplay observation. Preserve the correction, but remove/disable that ineffective speed-write path as part of the new experiment; do not treat derivation readback as speed selection.

BWAPI's `AutoMenuManager::onMenuFrame()` handles `GLUE_CREATE_MULTI` by choosing a map and game type, then posting the `Create` dialog's control-12 OK hotkey. This identifies the UI timing boundary but is not an integration dependency. Read-only inspection of the installed 1.16.1 executable and bundled `Broodwar.map` identifies `gluCustm_Interact` at `0x004AF5C0`, `loadMenu_gluCustm` at `0x004AF6D0`, and `CreateGame` at `0x004D3FC0`; the latter invokes `SNetCreateGame` at `0x004D409E`. These are candidate trace anchors, **not a proven call chain or validated speed field**. Neither the source nor the inspected disassembly yet establishes which creation value is authoritative or when the Local PC host copies it into the advertised room.

## Goals / Non-Goals

**Goals:**

- Force the creation-time speed consumed by the host to index `6` before network room creation, independently of the UI control, with a guarded one-shot write and auditable refusal.
- Keep the existing Fastest latency-table correction, while separating lobby speed selection from timing derivation.
- Establish that room advertisement, both clients' gameplay pacing and pluto's latency probe agree; an in-process readback is not sufficient.

**Non-Goals:**

- Driving the slider, editing BWAPI's auto-menu, changing saved preferences, patching the executable on disk, or changing network latency / the speed-modifier table.
- Claiming the known late `GameSpeed` address is the authoritative pre-lobby creation value without tracing it.

## Decisions

### D1: Trace the creation-speed consumer before choosing a hook

Use the Local PC map-OK path as the temporal anchor; trace the native OK transition to the room-creation call and identify the value that actually reaches creation or advertisement. Inspect the branch that consumes the chosen speed, its copies into any create-game structure, and the point of no return at `SNetCreateGame`. Compare Slowest and Fastest starts to confirm the candidate field and ordering, including a read-only runtime trace/watchpoint if static control/data flow is ambiguous. Record code bytes, calling convention, lifecycle across consecutive games, and whether BWAPI already patches the site. **Do not treat `0x004D3FC0`, `0x004D409E`, or `0x006CDFD4` as an approved detour/write address merely because they are known symbols.** Do not commit to a write point until these checks identify an authoritative host-side value before advertisement.

Alternative: write `GameSpeed` before posting OK or in the existing derivation detour. Rejected as unproven: the observed detour value reverts before gameplay, and the map-OK path may read a different saved/create-game value afterward. Alternative: operate the speed slider. Rejected because user-selected UI state is not a reliable forcing contract.

### D2: Gate a one-shot host creation override, not a peer runtime override

Once D1 identifies the consumer, install only a version/signature-validated, process-lifetime hook or bounded pre-call override in the SmartLoader DLL. Limit it to supported multiplayer **host creation**, verify original speed is in `0..6`, set the authoritative creation value to `6` before the native create/advertise operation, and log old/new or refusal. A no-op when already `6` is expected. Do not force speed in joiners or periodically poll/write live speed. Keep the existing derivation detour for the independent Fastest turn-length correction; retire its experimental speed write to avoid concealing failure of the pre-lobby selection. Guard installation and repeated matches against duplicate hooks, stale pointers, and unexpected BWAPI patches. If the override cannot be validated, leave original engine flow intact and mark the room unvalidated rather than claiming Fastest.

Alternative: write `GameSpeed` on both clients after room creation. Rejected because it cannot make the advertised lobby Fastest and risks host/peer disagreement.

### D3: Require room-level and end-to-end proof before promotion

Begin with deliberately non-Fastest host settings, then compare lobby speed displayed on host and peer, creation logs, gameplay speed and measured frame pacing, corrected turn table/scheduler, unmodified network latency, and pluto's four-frame verdict. Repeat with an already-Fastest host selection and a second auto-hosted room; finish a full two-client match. An unmatched room/game speed, desync, crash, drop, or probe rejection blocks promotion even if the host-side write read back `6`. Keep the known-good DLL available throughout.

Alternative: accept the creation-value readback or one client's lobby text as proof. Rejected because neither demonstrates negotiated speed and both clients' actual gameplay behavior.

## Risks / Trade-offs

- **Native creation field/call path not yet identified** -> make identification and verification the first gated task; refuse implementation of a guessed write address.
- **OK hotkey is asynchronous and BWAPI's menu hook runs after the old draw callback** -> hook the proven engine consumer before `SNetCreateGame`, not a timer or arbitrary pre-OK write.
- **Engine copies or replaces speed data during creation/start** -> observe successive copies and compare room/in-game speed on host and peer; do not promote a local-only effect.
- **Existing experimental late speed write masks the result** -> disable it in the test build while retaining Fastest latency correction; keep separate distinguishable logs and DLLs.
- **Unsupported build, repeated games or unexpected hook collision** -> validate signatures and state, refuse unsafe writes, log each creation separately, and retain removable SmartLoader registration for rollback.
- **Overlapping in-progress detour-speed spec** -> treat this as an alternative to, not completion of, the prior change; reconcile or supersede its Normal-lobby acceptance before syncing or archiving speed-selection specs.

## Migration Plan

1. Preserve known-good DLL and existing host/peer logs; use an experimental x86 build and distinguishable logs for this approach.
2. Complete the read-only D1 trace and record verified host-only hook site, speed source, byte signature, ordering and failure path before any patch. If no reliable pre-advertisement value is found, stop the experiment rather than guessing.
3. Test the bounded override with a non-Fastest stored choice, then already Fastest, a full match, and consecutive automatic creation; deploy only after both clients' room and gameplay evidence agrees.
4. For failure, restore the known-good DLL or remove the plugin path from both SmartLoader profiles; verify stock behavior and no experimental application records after relaunch.
