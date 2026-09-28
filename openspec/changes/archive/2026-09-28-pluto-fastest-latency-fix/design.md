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

The original hypothesis, with `esi = 4` inferred from two measured data points,
was:

| Lobby speed | index | ms | derived turn frames | pluto measures |
|---|---|---|---|---|
| Normal | 3 | 67 | 3 | 4 (accepted) |
| Fastest | 6 | 42 | 5 | 6 (rejected) |

pluto's measured value was thought to be the engine turn length plus one. A
write-disabled live Fastest run has now disproved that hypothesis: the plugin
recorded speed index `6` and `LatencyFrames[6] = 2` (the complete table was
`{1,1,1,1,2,2,2}`), while pluto reported `action latency is 6 frames here,
trained for 4`. The speed-modifier table remained the validated
`{167,111,83,67,56,48,42}` and no plugin write occurred. Therefore the raw
latency-frame table entry is not, by itself, the state that pluto measures.

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

### D1: Retire direct live scheduler writes

The initial `Fastest`/`LatencyFrames[6] = 3` decision is withdrawn. Clean,
write-disabled calibration establishes the relevant live state: at Normal,
speed index `3` had selected table and scheduler values of `1`, and pluto
measured `4` frames; at Fast, speed index `4` had values of `2`, and pluto
measured `6` frames. Writing `LatencyFrames[6] = 1` and the live scheduler
field at `0x0051CEA0 = 1` made pluto's probe measure 4 frames in a local test,
but two-client testing still observed a 6-frame probe and immediate AI drops.
The direct live scheduler write is therefore retired as session-unsafe. The
plugin MUST remain read-only until a pre-initialization correction mechanism is
identified and validated. It MUST NOT write network latency at `0x006556E4`.

Alternatives rejected: retaining the disproven target `3`, changing the ms
table blindly (which changes pacing), changing network latency (protocol-visible),
or continuing to write the live scheduler after initialization. The next phase
must identify a safe point before the engine copies derived timing into active
scheduler state.

### D1a: Observe the initialization boundary at startup frequency

The bot probes during the match opening, while active scheduler state becomes
available only after initialization. The watcher polls every 10 ms until it
observes the first valid nonzero table and scheduler state, then returns to its
100 ms diagnostic interval. Logs include monotonic timestamps for startup
waiting and timing-state readiness, allowing the operator to correlate the
initialization boundary with pluto's opening probe. This phase is read-only.

### D1b: Validated derivation boundary for read-only investigation

Static inspection of the installed 1.16.1 `StarCraft.exe` identifies the
per-speed derivation routine at `0x004D92A0`. It clears all seven
`LatencyFrames` entries at `0x004D92FD–0x004D9321`, then iterates at
`0x004D932E–0x004D9354`, calculating `1000 / (GameSpeedModifiers[i] * esi)`
and storing each result in `0x0051CE70..0x0051CE88`. This is the candidate
pre-initialization boundary to observe. Its exact ordering relative to pluto's
opening probe remains unproven; no hook or write is authorized from this
disassembly evidence alone. Read-only runtime records include BWGame's validated
frame counter at `0x0057F23C` (`0x0057F0F0 + 0x14C`) beside the first initialized
timing-state transition, so it can be correlated with pluto's `issued F...` and
`applied F...` samples without a code hook.

The Fastest diagnostic run recorded the first initialized state at game frame
`0` with selected table and scheduler values of `2`; pluto then issued its test
commands at frame `6` and observed effects at frame `12`. The derivation
boundary therefore completes before the bot probe. This proves the timing order
required for task 6.1, but it does not validate a safe mechanism to affect the
derivation result.

### D1c: Candidate derivation-time interception, gated by exact signature validation

The candidate mechanism is an in-memory, reversible detour at the derivation
routine entry `0x004D92A0`. Before installation, the DLL must validate the
expected 1.16.1 instruction bytes at the overwritten entry range and refuse on
any mismatch. The detour must execute the original derivation through a
trampoline, then set only `LatencyFrames[6]` to `1` before returning to the
engine. This occurs at frame 0, before pluto's frame-6 probe and before normal
scheduler use.

The detour must restore page protection after installation and flush the
instruction cache. It is process-lifetime: it remains installed until StarCraft
exits, when Windows reclaims the trampoline and original image; it MUST NOT try
to uninstall from `DllMain` or suspend game threads during unload. It must not
modify `GameSpeed`, `GameSpeedModifiers`, network latency, or the live scheduler
field `0x0051CEA0`. Both clients must run identical validated DLLs before
two-client testing.

Alternatives rejected: changing `GameSpeedModifiers[6]` (changes Fastest
pacing), changing the derivation multiplier for every speed, and post-
initialization table or scheduler writes (already shown unsafe).

The verified entry signature is `55 8B EC 83 EC 24` (`push ebp; mov ebp, esp;
sub esp, 0x24`). The minimum safe overwrite is 6 bytes: a 5-byte `JMP rel32`
cannot end after the first byte of the six-byte stack-allocation instruction.
The trampoline resumes at `0x004D92A6`.

### D2: A DLL loaded into the client process, observing continuously

The DLL samples state through the match opening, when pluto performs its latency
probe, and records every observable state transition. Once a correction has
been validated, a short-interval re-apply (read, compare, write only on mismatch)
can make it self-healing across matches and resilient to the engine zeroing the
table (`0xD92FF` zeroes all seven entries before the derivation runs).

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

### D7: Calibrate from repeated, phase-aware live measurement before selecting a target

The initial Fastest dry-run established that the former assumed table values did
not predict pluto's verdict. Clean, initialized comparison then identified the
predictive relationship: Normal's selected table and scheduler values of `1`
produced pluto's `4`-frame measurement, while Fast's values of `2` produced
its `6`-frame measurement. The correlation identifies a candidate value, not an
authorized write target: initialized live writes are retired pending a safe
pre-initialization mechanism.

## Calibration record

The former decision to force speed index `6` (`Fastest`) and hold
`LatencyFrames[6]` at `3` is rejected. In clean, write-disabled runs, Normal
recorded speed `3`, selected table and scheduler values `1`, and pluto's
accepted `4`-frame verdict; Fast recorded speed `4`, values `2`, and pluto's
rejected `6`-frame verdict. The selected table and scheduler values therefore
predict the observed boundary. The authorized target for controlled validation
is `GameSpeed = 6` with `LatencyFrames[6] = 1` and scheduler turn length
`0x0051CEA0 = 1`. A first write-path attempt changed only the table and reached
the probe with scheduler value `2`, so it retained pluto's `6`-frame result;
both initialized values must be changed together.

### Recorded runtime findings

- Clean write-disabled baselines: Slow (index 2) and Normal (index 3) selected
  value `1` and did not report a mismatch; Normal logged four 4-frame samples.
  Fast (index 4), Faster (5), and Fastest (6) selected value `2` and logged
  four 6-frame samples.
- A local initialized write of table and scheduler value `1` produced four
  4-frame samples. The bot then dropped before emitting its final verdict.
- With the same initialized write applied on both `StarCraft-SL.pluto.exe` and
  `StarCraft-SL.exe`, both plugins read back value `1`; pluto still measured six
  frames and the AI dropped immediately. Direct initialized writes are unsafe.
- SmartLoader profiles proven in use are `mods.pluto.txt`/
  `StarCraft-SL.pluto.exe` for the bot and `mods.txt`/`StarCraft-SL.exe` for the
  peer. Launcher executable names are logged but are not used as a whitelist;
  the memory signature controls access.

### Completion decision and deferred validation

The change is accepted as complete for its core outcome: both clients loaded
the same signature-validated derivation detour, derived Fastest table and
scheduler values of `1` at startup, and pluto recorded `action latency OK: 4
frames (4 samples)`. The original broad calibration checklist, a full-match
integrity run, broader regression evidence, and a documented rollback run are
intentionally deferred rather than represented as passed tasks.

Operational risk remains: the completed two-client run demonstrated compatible
startup and latency probing, but did not establish full-match stability through
to a natural game conclusion. Operators should retain the ability to remove the
plugin path from both SmartLoader profile files to restore stock behavior.

## Risks / Trade-offs

- **A derivation-time change can still break two-client integrity** -> install
  the same validated DLL in both clients and require a controlled full-match
  validation before enabling the mechanism for normal use.
- **The engine re-derives or zeroes the table after the write** -> continuous
  re-apply, plus log evidence per match.
- **The calibrated target could fail at Fastest** -> wait for nonzero initialized
  timing state, apply both `Fastest`/`1` values in one controlled local match,
  and restore dry-run or unregister the DLL immediately if the bot does not
  measure 4 frames.
- **The valid state can appear too late for the bot probe** -> poll every 10 ms
  during startup, log the first-valid transition and application timestamps,
  then compare them with the bot's probe evidence before changing the target.
- **An incorrect detour can corrupt client execution** -> require exact code
  signature validation, preserve every overwritten instruction in a trampoline,
  restore protection, flush the instruction cache, and refuse rather than patch
  an unrecognised binary.
- **Uninstall races with a game thread executing the detour** -> retain the
  validated detour for the host process lifetime and rely on process termination
  for memory reclamation; never uninstall under the loader lock.
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
2. Validate with writes disabled: launch through SmartLoader at every target
  lobby speed, capture plugin samples through pluto's opening probe, and pair
  them with pluto's recorded verdict.
3. Select and implement a write target only after the samples demonstrate a
  predictive relationship; then validate it in a short match before peer tests.
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
- Which engine state or phase transition is the input to pluto's empirical
  latency probe, and whether it identifies a safe local correction.
