# Design

## Context

See `proposal.md` for motivation. What shapes the approach is how the engine
actually produces latency, which we established from the shipped binaries rather
than from documentation.

### The engine couples frame pacing and latency

`Starcraft-BWAPI.exe` (1.16.1.1) derives its per-speed **turn length in frames**
from the per-speed **millisecond table** at game start:

```
   GameSpeedModifiers[i]  @0x005124D8          LatencyFrames[i]  @0x0051CE70
   {167,111,83,67,56,48,42}                    runtime-derived, 7 x u32
        |                                            ^
        |   derivation (code at RVA 0xD92A0)         |
        +--------------------------------------------+
            LatencyFrames[i] = 1000 / (GameSpeedModifiers[i] * esi)
            integer division, floored to a minimum of 1
            esi = clamped parameter, valid range 1..19 (20 = disabled)
```

and drives turns from it (code at RVA `0xD95B2`):

```
   [0x51CE94] += GameSpeedModifiers[i] / 2            ; deadline advances one frame
   [0x51CE94]  = now + GameSpeedModifiers[i] * LatencyFrames[i] / 2   ; catch-up reset
   [0x51CEA0]  = LatencyFrames[i]                     ; current turn length, in frames
```

So the frame duration is `ms[i] / 2`, the turn period is
`ms[i] * LatencyFrames[i] / 2` milliseconds, and the turn length in frames is
`LatencyFrames[i]`. There is no independent "latency setting" to tune: changing
the ms table changes both pacing and latency.

### What pluto requires

pluto probes latency empirically in the opening window (it issues a command and
observes when the effect lands), compares it to the value it was trained for
(4 frames), and disables its latency model - "micro will be off" - on mismatch.
It does not use BWAPI's reported latency, which is computed differently.

With `esi = 4` (inferred from two measured data points), the current state is:

| Lobby speed | index | ms | derived turn frames | pluto measures |
|---|---|---|---|---|
| Normal | 3 | 67 | 3 | 4 (accepted) |
| Fastest | 6 | 42 | 5 | 6 (rejected) |

pluto's measured value is the engine turn length plus one; the "+1" is consistent
across both observations and is treated as a calibration target to confirm, not
as a proven constant.

### Falsified alternatives (evidence from the running install)

- `/speed <ms>` writes the ms table for all seven speeds. At Fastest it is a
  no-op (42 is already the value); at other speeds it changes pacing, and if it
  lands before the derivation it also rewrites the latency table.
- `speed_override = 42` makes BWAPI write the ms table at match start, which
  either relabels every speed's turn length or creates a pacing asymmetry with
  the unmodified peer client. Observed result: the pluto instance dies.
- `LatencyChanger` patches the networked latency setting (`Latency` @0x006556E4,
  0..2, propagated by `CMDRECV_SetLatency`), which is a protocol-visible game
  setting and incompatible with BWAPI's bootstrap. Observed result: crash.
- A second AI module is not loaded through BWAPI itself; SmartLoader provides
  only the required DLL loading step.
- An external patcher process is possible in principle, but the game runs
  elevated (access denied to `OpenProcess` from a normal shell) and it would not
  solve the peer-client question.

### Constraints

- The image has `RELOCS_STRIPPED` and loads at `0x400000`, so absolute addresses
  are stable: no ASLR handling, no pattern search needed at runtime.
- `StarCraft.exe` and `Starcraft-BWAPI.exe` are byte-identical at the derivation
  and scheduler code sites we read, so one address set covers both clients. They
  differ only by the 37-byte `.bwapi` bootstrap section BWAPI's own loader adds.
- SmartLoader provides only the DLL loading step for this change. The plugin's
  DLL path is written to `C:\Starcraft\mods.BWAPI.txt` and the client is
  launched with `C:\Starcraft\StarCraft-SL.BWAPI.exe`. The plugin then reads
  and writes the StarCraft binary's own memory; no external injector or helper
  process is involved. `C:\Starcraft\SmartLoader.BWAPI.log` is used only to
  confirm the launch and DLL load.
- Upstream BWAPI source confirms these 1.16.1 addresses in
  `bwapi/BWAPI/Source/BW/Offsets.h` and the speed constants in
  `bwapi/BWAPI/Source/BW/Constants.h`: `GameSpeed` at `0x006CDFD4`,
  `GameSpeedModifiers` at `0x005124D8`, `LatencyFrames` at `0x0051CE70`, and
  `Latency` at `0x006556E4`. Build tooling is Visual Studio x86 and CMake.

## Goals / Non-Goals

**Goals:**

- Force the local game speed to `Fastest` and hold the engine turn length at the
  value that makes pluto observe its trained action latency.
- Get that code into the client reliably, per match, without changing how the
  match is configured or what the peer sees.
- Make the whole thing evidence-driven: the target value comes from measurement,
  and every application is logged.

**Non-Goals:**

- Modifying pluto, BWAPI, `bwapi.ini` semantics, or the engine's executable on
  disk.
- Changing the exchanged latency setting or any part of the network protocol.
- Supporting client versions other than the validated 1.16.1.1 build.
- Fixing the same problem for other bots, or making the fix general-purpose.

## Decisions

### D1: Force Fastest and adjust the derived turn-length table, not network latency

Force the local game speed by writing `GameSpeed = 6` at `0x006CDFD4`, then
write `LatencyFrames[6] = 3` at `0x0051CE70` after the engine has derived it.
This makes pluto observe its trained `4`-frame action latency. The plugin does
not call BWAPI's `setLocalSpeed()` command; it operates directly on the host
process memory.
Alternatives rejected: changing the ms table (also changes pacing - the failed
`speed_override` route), changing `Latency` (protocol-visible, peer-visible, and
cannot reach the needed frame count anyway because the value range is 0..2), and
changing the derivation input `esi` (we do not control its source, and it moves
every speed's turn length, not just the target's).

Consequence: the target speed's turn period becomes shorter than stock
(3 x 21 ms = 63 ms instead of 5 x 21 ms = 105 ms at Fastest). This is inherent -
the stock value is exactly what pluto rejects - and is the one behavioural
difference from a stock Fastest match.

### D2: A DLL loaded into the client process, re-applying continuously

The DLL holds the value rather than writing once: the engine re-derives the table
at each match start, and sessions run back-to-back matches. A short-interval
re-apply (read, compare, write only on mismatch) makes the fix self-healing
across matches and resilient to the engine zeroing the table (`0xD92FF` zeroes
all seven entries before the derivation runs).

Alternatives rejected: a one-shot write (loses the fix on the next match), a code
patch of the derivation routine (more invasive, harder to validate, and would
have to be undone for other speeds).

### D3: Load the self-memory plugin through SmartLoader

Write the native plugin's DLL path to `C:\Starcraft\mods.BWAPI.txt` and launch
`C:\Starcraft\StarCraft-SL.BWAPI.exe`. SmartLoader only loads the DLL. After
loading, the plugin validates the host and accesses the StarCraft binary through
its own address space. Confirm the load in
`C:\Starcraft\SmartLoader.BWAPI.log` and the plugin startup log.

Alternatives rejected: PE-section bootstrap generation, a separate runtime
injector, external process memory writes, `CreateRemoteThread` injection,
`SetWindowsHookEx`, and registry-based injection.

### D4: The peer client is addressed only if validation proves it is necessary

Phase 1 delivers the plugin to the bot's client only, forces its local speed to
Fastest, and validates a full match
against an unmodified peer. If the match is stable with the peer unmodified, the
fix stays local. If the cadence mismatch proves fatal, Phase 2 generates the same
bootstrap variant for the peer client, so both clients hold the same turn length.

Rationale: the turn length is a protocol-adjacent value, but the peer's client
never sees our table; whether a mismatch is tolerated is an empirical question
that a short match answers cheaply. Doing Phase 2 up front would be speculative
work with a broader blast radius.

### D5: Validate before writing, and log every decision

Before the first write of a session, the DLL checks a signature it can verify:
the host executable must be a validated StarCraft client, the ms table at
`0x005124D8` must equal `{167,111,83,67,56,48,42}`, the speed index at
`0x006CDFD4` must be in range, and the current turn length at
`0x0051CE70 + 4 * 6` must be a value the derivation can produce.
If the signature does not match, it writes nothing and logs that it refused. Every
application logs the speed index, the previous value, and the applied value.

Rationale: a wrong-address write into a game process is the worst failure mode
here, and the cost of verification is one read per second. This also satisfies the
"safe behaviour on unsupported state" requirement in `specs/game-turn-latency`.

### D6: Diagnostics are emitted by the in-process plugin log

The DLL always writes the observed engine state it relies on (speed index, ms
table, turn-length table, latency setting, current turn length, bot measurement
when available, and applied value) to its log, because that data is available
where the fix already runs. The same log records unsupported hosts, unexpected
signatures, and refusal reasons.

Rationale: the in-process path needs no separate process or cross-process
memory access and provides the evidence used by both calibration and runtime
validation. Removing the external reader keeps the delivery to one DLL and
avoids a second diagnostic executable that would require separate launch and
integrity-level handling.

### D7: Calibrate the target from measurement, then freeze Fastest/3 as the default

The default target is derived from evidence: the plugin forces Fastest (index 6)
and pluto measures the turn length plus one, so the turn length must be 3 to make
pluto observe 4. Because the "+1"
mapping is inferred from two observations rather than proven, the value is a
configurable option, and the first implementation task is to confirm it against a
live match (Phase 0) before any write path is enabled by default.

### Calibration record

The recorded calibration decision is to force speed index `6` (`Fastest`) and
hold `LatencyFrames[6]` at `3`. The evidence chain is the validated address
inspection and live bot measurements described by tasks 1.1 and 1.2: stock
`Normal` has engine turn length `3` and pluto measures `4`, while stock
`Fastest` has engine turn length `5` and pluto measures `6`. Thus the observed
relationship is `bot latency = engine turn length + 1`; applying `3` at
`Fastest` restores the trained `4`-frame value. This remains subject to the
live-match verification in tasks 1.1 and 1.2; the plugin refuses unsupported
memory signatures rather than treating this record as permission to write an
unvalidated client.

## Risks / Trade-offs

- **Peer desync or drop if the turn cadence must match between clients** ->
  Phase 1 validates with a short match; escalate to D4 Phase 2 if it fails, and
  never ship a half-symmetric configuration.
- **The engine re-derives or zeroes the table after the write** -> continuous
  re-apply, plus log evidence per match.
- **The "+1" mapping is wrong, so the target value is off by one** -> Phase 0
  calibration against a live client and pluto's own log; target is configurable.
- **A different client build is targeted by mistake** -> signature validation
  before writing, refusal logged, never a blind write.
- **The write lands after pluto's opening probe** -> apply as early as the speed
  index becomes valid, re-apply at high frequency, and log whether the value was
  already correct at probe time.
- **Shortened turn period stresses the network or the peer** -> measure a full
  match for stalls; revert to the unmodified executable if unstable.
- **Antivirus or SmartScreen flags the injected DLL** -> document it; keep the
  change minimal; disabling the DLL path in `mods.BWAPI.txt` restores stock
  launch behavior.
- **Plugin logging is unavailable because the host refuses the DLL** -> the
  SmartLoader and plugin startup records document the refusal, and the stock
  client remains unmodified.
- **Maintenance burden of a duplicated bootstrap generator** -> see open
  questions; the alternative is upstreaming to the sibling BWAPI repository.

## Migration Plan

1. Build the 32-bit DLL into a directory outside the game install.
2. Validate with writes disabled first: launch through SmartLoader, read the
  SmartLoader and plugin logs, confirm host validation passes, and confirm the
  observed values match expectation.
3. Enable the write path, play one short match, and confirm from pluto's log that
   it reports no latency mismatch and keeps micro enabled.
4. Keep the original StarCraft executable unchanged and use the SmartLoader
  launch path for normal play.
5. Rollback: remove or disable the DLL path in `mods.BWAPI.txt` and launch the
  stock client. No executable is overwritten, so rollback is immediate.

## Open Questions

- Whether SmartLoader accepts an absolute or relative DLL path in
  `C:\Starcraft\mods.BWAPI.txt`; the implementation task must use the supported
  form and verify it in `C:\Starcraft\SmartLoader.BWAPI.log`.
- Whether the peer client should load the same DLL once Phase 2 is proven
  necessary for symmetry.
- Whether the target speed should be configurable beyond Fastest once the
  mapping is confirmed (the design already leaves other speeds untouched).
