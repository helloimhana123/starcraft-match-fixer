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
- A second AI module is impossible: BWAPI loads exactly one, and this BWAPI build
  has no plugin loader at all (no plugin strings, no ini key, no plugins
  directory).
- An external patcher process is possible in principle, but the game runs
  elevated (access denied to `OpenProcess` from a normal shell) and it would not
  solve the peer-client question.

### Constraints

- The image has `RELOCS_STRIPPED` and loads at `0x400000`, so absolute addresses
  are stable: no ASLR handling, no pattern search needed at runtime.
- `StarCraft.exe` and `Starcraft-BWAPI.exe` are byte-identical at the derivation
  and scheduler code sites we read, so one address set covers both clients. They
  differ only by the 37-byte `.bwapi` bootstrap section BWAPI's own loader adds.
- `starcraft-bwapi-loader.exe` in the game folder is a LIEF-based bootstrap
  generator: it validates `StarCraft.exe` (1.16.1, PE32, no dynamic base, imports
  `LoadLibraryA`) and appends a startup section that calls
  `LoadLibraryA("bwapi-data\\BWAPI.dll")`. That is the established, in-repo way to
  get code into the client, and it is the technique this design reuses.
- Build tooling available on this machine: Visual Studio 2022, CMake, and the
  sibling checkout `C:\Programming\starcraft-bwapi` whose build tree already
  vendors LIEF. Python 3.13 is present but has no LIEF binding.

## Goals / Non-Goals

**Goals:**

- Hold the engine turn length for the target game speed at the value that makes
  pluto observe its trained action latency, with the lobby game speed untouched.
- Get that code into the client reliably, per match, without changing how the
  match is configured or what the peer sees.
- Make the whole thing evidence-driven: the target value comes from measurement,
  and every application is logged.

**Non-Goals:**

- Modifying pluto, BWAPI, `bwapi.ini` semantics, or the engine's executable on
  disk.
- Changing the room's game speed, the exchanged latency setting, or any part of
  the network protocol.
- Supporting client versions other than the validated 1.16.1.1 build.
- Fixing the same problem for other bots, or making the fix general-purpose.

## Decisions

### D1: Adjust the derived turn-length table, not pacing and not the latency setting

Write `LatencyFrames[targetIndex]` in-process, after the engine has derived it.
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

### D3: Load the DLL by bootstrap-generated executables, not a runtime injector

Reuse the established technique: generate a client executable whose startup
section calls `LoadLibraryA` on our DLL after BWAPI's. This gives deterministic
load order, needs no injector, works for a process that is launched elevated, and
touches no existing file - the original executables stay in place, so rollback is
launching the original.

Alternatives rejected: `CreateRemoteThread` injection (an extra moving part,
needs matching integrity level, and can be blocked), `SetWindowsHookEx`
(requires a message pump and behaves poorly with the loader's bootstrap), and
registry-based injection (system-wide, admin-only, disproportionate).

### D4: The peer client is addressed only if validation proves it is necessary

Phase 1 delivers the DLL to the bot's client only and validates a full match
against an unmodified peer. If the match is stable with the peer unmodified, the
fix stays local. If the cadence mismatch proves fatal, Phase 2 generates the same
bootstrap variant for the peer client, so both clients hold the same turn length.

Rationale: the turn length is a protocol-adjacent value, but the peer's client
never sees our table; whether a mismatch is tolerated is an empirical question
that a short match answers cheaply. Doing Phase 2 up front would be speculative
work with a broader blast radius.

### D5: Validate before writing, and log every decision

Before the first write of a session, the DLL checks a signature it can verify:
the ms table must be the expected seven values, the speed index must be in range,
and the current turn length must be one of the values the derivation can produce.
If the signature does not match, it writes nothing and logs that it refused. Every
application logs the speed index, the previous value, and the applied value.

Rationale: a wrong-address write into a game process is the worst failure mode
here, and the cost of verification is one read per second. This also satisfies the
"safe behaviour on unsupported state" requirement in `specs/game-turn-latency`.

### D6: Diagnostics are in-process first, external reader second

The DLL always writes the observed engine state it relies on (speed index, ms
table, turn-length table, applied value) to its log, because that data is free
where the fix already runs. A small read-only reader tool inspects a chosen
client from outside for ad-hoc checks, and reports access problems plainly - it
will usually need to run at the game's integrity level.

Rationale: the in-process path always works and needs no elevation; the external
path covers the case where nothing is loaded yet or the operator wants a
second opinion.

### D7: Calibrate the target from measurement, then freeze it as a default

The default target value is derived from evidence: pluto measures the turn length
plus one, so the turn length must be 3 to make pluto observe 4. Because the "+1"
mapping is inferred from two observations rather than proven, the value is a
configurable option, and the first implementation task is to confirm it against a
live match (Phase 0) before any write path is enabled by default.

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
- **Antivirus or SmartScreen flags a bootstrap-modified executable** -> document
  it; keep the change minimal; the original executables remain available.
- **Tooling integrity level mismatches (game runs elevated)** -> the in-process
  DLL needs no elevation; only the external reader does, and it reports denial
  instead of failing silently.
- **Maintenance burden of a duplicated bootstrap generator** -> see open
  questions; the alternative is upstreaming to the sibling BWAPI repository.

## Migration Plan

1. Build the DLL and the bootstrap generator into a directory outside the game
   install; generate the variant executables into a staging folder.
2. Validate with `run` mode disabled first: launch the variant, read the log,
   confirm the signature check passes and the observed values match expectation.
3. Enable the write path, play one short match, and confirm from pluto's log that
   it reports no latency mismatch and keeps micro enabled.
4. Copy the variant executables into the game install and use them for normal
   play.
5. Rollback: launch the original `Starcraft-BWAPI.exe` and `StarCraft.exe`. No
   installed file is overwritten, so rollback is immediate and complete.

## Open Questions

- Whether to extend the sibling `starcraft-bwapi` loader to accept a list of
  DLLs, or keep a self-contained generator in this repository. Both reuse the
  same technique; the choice affects packaging only.
- What the variant executables should be named, and whether the peer client
  should get one unconditionally for symmetry once Phase 2 is proven stable.
- Whether the target speed should be configurable beyond Fastest once the
  mapping is confirmed (the design already leaves other speeds untouched).
