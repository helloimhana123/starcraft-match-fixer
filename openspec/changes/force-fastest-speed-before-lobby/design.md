# Design

## Reconciliation with the in-progress derivation-detour change

`force-fastest-speed-in-derivation-detour` accepts a room that still advertises
Normal as long as both clients play at in-game Fastest. That acceptance is
superseded by this change's pre-lobby requirement: the host now forces Fastest
into the creation-speed byte before the room is advertised, and the verified
Slowest-host run showed both clients displaying Fastest. The older change's
`game-speed-selection` delta and `design.md` are annotated as superseded and
must not be promoted as an independent speed-selection success. There is one
supported contract: pre-lobby Fastest creation via the verified `+0x26` byte,
retaining the Fastest turn-table correction.

## Context

See `proposal.md` for motivation and `specs/pre-lobby-game-speed-selection/spec.md` for the behavioral contract. The current x86 SmartLoader plugin in `src/plugin.cpp` already installs a signature-gated detour around the engine's turn-length derivation at `0x004D92A0` and corrects `LatencyFrames[6]` to `1`. Its experimental speed write at `0x006CDFD4` happens too late: a Slowest-start test logged `0 -> 6` at derivation and `0` again at first gameplay observation. Preserve the correction, but remove/disable that ineffective speed-write path as part of the new experiment; do not treat derivation readback as speed selection.

BWAPI's `AutoMenuManager::onMenuFrame()` handles `GLUE_CREATE_MULTI` by choosing a map and game type, then posting the `Create` dialog's control-12 OK hotkey. This identifies the UI timing boundary but is not an integration dependency. Read-only inspection of the installed 1.16.1 executable and bundled `Broodwar.map` identifies `gluCustm_Interact` at `0x004AF5C0`, `loadMenu_gluCustm` at `0x004AF6D0`, and `CreateGame` at `0x004D3FC0`; the latter invokes `SNetCreateGame` at `0x004D409E`. These are candidate trace anchors, **not a proven call chain or validated speed field**. Neither the source nor the inspected disassembly yet establishes which creation value is authoritative or when the Local PC host copies it into the advertised room.

### Diagnostic trace experiment (runtime validation pending)

Static inspection of the installed 1.16.1 image narrows the candidate path:
`gluCustm_CustomCtrl_InitializeChildren` copies the low byte of `GameSpeed` to
`0x0059BB6C` at `0x004AF5B4`, and the map-OK path passes that byte as an
argument to `SelectMapOrEntry` (`0x004AF219` -> `0x004A8050`). The dialog can
also update `0x0059BB6C` at `0x004AE274`. In `SelectMapOrEntry`, the value at
the corresponding argument is passed to `0x004A68D0` at `0x004A82C1`. That
routine may write byte `6` or the supplied value to create-data offset `0x26`
(`0x004A694E`, `0x004A696E`, `0x004A699C`), then stores the argument at
`GameSpeed` at `0x004A69E5`. The routine can next call `CreateGame` at
`0x004A8301`, and `CreateGame` calls the `SNetCreateGame` import at
`0x004D409E`. Another path (`0x004DC024`) copies `GameSpeed` to its own
create-data offset `0x26`; this has **not** been established as the Local PC
map-OK path. None of these static observations proves which branch executes
for an auto-hosted Local PC room or which value the peer sees.

The optional `PLUTO_FASTEST_LATENCY_TRACE_CREATION=1` experiment in
`src/plugin.cpp` checks whole-instruction entry signatures and the
`SNetCreateGame` call-site signature before installing diagnostic-only hooks.
It logs the selection argument, before/after creation-data bytes, CreateGame
buffer bytes and SNet call outcome without a new speed write. A bounded event
buffer defers log I/O to the watcher thread. This is an investigation aid, **not
task 1.2 or 1.3 completion**: compare deliberate Slowest and Fastest rooms on
both clients before authorizing an override.

#### First operator-run Slowest lobby (2026-09-27)

With the host's prior selection Slowest, the operator reported that the lobby
displayed **Slowest**; they did not manually start gameplay. Host trace
`C:\Starcraft\PlutoCreationTrace.server.20260927-172500.log` recorded both
hooks installed and `select-map argument=0 selection_byte=0 game_speed=0
mode=1`, followed by `create-data-before/after argument=0 field26=0
field27=2 result=0` from caller `0x004A82C6`. Neither `create-game-before`
nor `snet-before` appeared. The peer trace contained only startup/installation
records, so its lobby speed was not captured in the plugin; the user report
does not separately identify a peer lobby display. The host log later observed
`frame=48 speed=0`, and the same-time `pluto.log` recorded four 4-frame action
samples and `onEnd`; auto-menu appears to have entered a game despite no manual
start. These game records **do not** establish Fastest speed or pre-lobby
success.

The missed branch is identifiable statically: when the byte at `0x0057F0B4`
is nonzero, `SelectMapOrEntry` calls `CreateLadderGame` (`0x004D3910`) at
`0x004A82E7` rather than `CreateGame` at `0x004A8301`. `CreateLadderGame`
calls the distinct `SNetCreateLadderGame` import at `0x004D3B0B`. Diagnostic
hooks now cover both creation paths and Storm calls. A subsequent operator-run
Slowest/Fastest comparison must still show the branch reaching room creation,
the pre-advertisement value, and both clients' lobby observations. No
creation-speed write is authorized from the current trace.

#### Second operator-run comparison: Slowest versus Fastest (2026-09-27)

The operator ran separate Slowest and Fastest host/peer sessions using the
expanded diagnostic build. They confirmed **both clients' lobby displays**
matched the selected speed in each run (Slowest for the first room, Fastest for
the second). The server logs, respectively
`PlutoCreationTrace.server.20260927-172929.log` and
`PlutoCreationTrace.server.20260927-173050.log`, report the same observed host
path and call site:

| Stage | Slowest host | Fastest host |
| --- | --- | --- |
| `select-map` at caller `0x004AF21E` | argument/selection/`GameSpeed` = `0/0/0` | `6/6/6` |
| `create-data-before` at caller `0x004A82C6` | argument `0`, offset `0x26` = `0`, offset `0x27` = `2` | argument `6`, offset `0x26` = `0`, offset `0x27` = `2` |
| `create-data-after` | result `0`, offset `0x26` = `0`, `GameSpeed` = `0` | result `0`, offset `0x26` = `6`, `GameSpeed` = `6` |
| `create-ladder-before` at caller `0x004A82EC` | offset `0x26` = `0` | offset `0x26` = `6` |
| `snet-ladder-before/after` at `0x004D3B0B` | offset `0x26` = `0`, Storm result `1` | offset `0x26` = `6`, Storm result `1` |

Both runs recorded mode byte `1`, game type argument `65538` at the Storm
call, and no diagnostic speed write. The client logs show successful plugin
startup but no creation hook (expected for joiners); the peer lobby observations
are operator-reported rather than read from plugin memory. Static inspection
also shows `CreateLadderGame` updates other creation fields (`+0x24` and
`+0x25`) before the Storm call, but the captured `+0x26` value survives that
path in both cases. Thus the host's selection and candidate creation byte
correlate with the advertised room in the two tested values.

### D1 resolution and the implemented override

The evidence identifies one authoritative host-side value before advertisement:
the create-data byte at buffer `+0x26`, written by `0x004A68D0` at `0x004A699C`
from the chosen speed, carried unchanged through `CreateLadderGame`, and present
in the buffer handed to the Storm create call. Both tested host values (`0`,
`6`) survived to `snet-ladder-before`, and both clients' lobby displays matched.
The last mutating point on this path is `0x004A68D0`; nothing between it and the
Storm call rewrites `+0x26` (the routine copies 32 bytes to `+0x6D` and sets
`+0x24`/`+0x25`). The peer never reaches the creation hooks, so the effect is
host-only.

`src/plugin.cpp` therefore installs the creation hooks whenever the override or
verbose trace is enabled. After `0x004A68D0` returns, `apply_creation_speed_override`
reads the buffer captured in the entry hook and:
writes `6` when the original byte is `0..5` (`override-applied`), leaves `6`
untouched (`override-noop-fastest`), and refuses inaccessible or `>6` values
(`override-refused-inaccessible`, `override-refused-range`, `override-refused-write`,
`override-refused-no-buffer`) without writing. It does not modify `GameSpeed`,
the speed-modifier or network-latency tables, the UI slider, joiner state, or
any file. The override is on by default; `PLUTO_FASTEST_LATENCY_FORCE_CREATION_SPEED=0`
leaves the hooks diagnostic-only. Consecutive-room lifecycle, the controlled
non-Fastest readback, and end-to-end lobby/gameplay agreement remain to be
validated in tasks 3.1-3.3 before promotion.

#### Override validation with host set to Slowest (2026-09-27)

Run log `PlutoCreationTrace.server.20260927-174444.log` shows the override in
effect. The host's prior selection was Slowest (`select-map argument=0`,
`game_speed=0`), `create-data-after` still read `field26=0`, then the new stage
`override-applied argument=0 result=6 field26=6` recorded the write, and
`snet-ladder-before`/`snet-ladder-after` carried `field26=6` and returned `1`.
The operator confirmed **both** clients' lobbies displayed **Fastest** and that
the in-game speed actually changed.

At first gameplay the host logged `frame=0 speed=6 table_turn=1
scheduler_turn=1 network_latency=0 speed_modifiers=167,111,83,67,56,48,42
latency_frames=1,1,1,1,2,2,1`; the peer logged `frame=1 speed=6` with the same
tables. `pluto.log` recorded four `4 frames` action samples, i.e. the accepted
probe, and the host won the match. A second host creation in the same process
(`creation=2`) again logged `override-applied argument=0 result=6` followed by a
successful Storm call, so the hook survives repeated creation.

Residual gaps before promotion: a run whose prior setting is already Fastest
(expect `override-noop-fastest`), a confirmed full second match, an unsupported
signature/refusal run, and the documented rollback. The host's post-creation
`observed ... frame=234 speed=0` in this log is not attributed to a validated
second match and is treated as unverified rather than a passed check.

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
