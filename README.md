# Pluto Fastest Latency Fix

This project builds a 32-bit DLL for the validated StarCraft 1.16.1 client and
a read-only diagnostic executable. The DLL is loaded by SmartLoader inside the
client; it does not patch an executable on disk and does not modify the network
latency setting. It forces the host's Local PC room to advertise Fastest before
room creation (see "Experimental pre-lobby Fastest override") and corrects the
Fastest turn length. It is still experimental: an already-Fastest run, a
confirmed consecutive match, and a documented rollback are not yet recorded, so
keep the profile-file rollback available.

## Build

Configure CMake with the Visual Studio x86 generator, then build the default
configuration. The outputs are `PlutoFastestLatencyFix.dll` and
`PlutoLatencyInspector.exe`.

## Diagnostic mode

Run `PlutoLatencyInspector.exe [PID]` while a client is running. It reads the
validated addresses and reports the speed index, millisecond table,
turn-length table, network latency setting, and whether Fastest/3 is active.
If access is denied, run it at the same integrity level as the elevated game.
The inspector is read-only. It cannot infer a bot measurement; that value must
come from the bot's own session log.

For a write-disabled plugin smoke test, set
`PLUTO_FASTEST_LATENCY_DRY_RUN=1` before launching SmartLoader. The plugin log
path can be selected with `PLUTO_FASTEST_LATENCY_LOG`; otherwise it is
`PlutoFastestLatencyFix.log` in the client working directory.

When correlating a bot session log during calibration, set
`PLUTO_BOT_MEASURED_LATENCY` to the integer latency reported by that session
before launching the client. The plugin records that value next to the live
engine tables; if it is not set, the diagnostic line explicitly reports the
measurement as unavailable rather than inferring one.

## SmartLoader play workflow

### Experimental pre-lobby Fastest override

This experimental build forces the host's Local PC room to advertise Fastest
before it is created, then keeps the existing Fastest turn-length correction. On
each host creation it writes index `6` into the verified create-data speed byte
(`+0x26`) after `0x004A68D0` returns, no-ops when the value is already `6`, and
refuses inaccessible or out-of-range values. It does not touch the UI slider,
`GameSpeed`, the speed-modifier or network-latency tables, joiner state, or any
file. Set `PLUTO_FASTEST_LATENCY_FORCE_CREATION_SPEED=0` to keep the hooks
diagnostic-only (no speed write); by default the override is armed.

Build the x86 Release DLL and install it in the existing server and client
SmartLoader profiles. After verifying the installed DLL matches the build, the
operator can run `C:\Starcraft\TestLatencyFix.ps1` to launch both clients with
separate timestamped trace logs. The script preserves previous logs and refuses
to start when a StarCraft client is already running. The active
launchers are `StarCraft-SL.pluto-server.exe` and
`StarCraft-SL.pluto-client.exe`; their profiles are `mods.pluto-server.txt` and
`mods.pluto-client.txt`.

Set `PLUTO_FASTEST_LATENCY_TRACE_CREATION=1` in each launcher's environment and
set `PLUTO_FASTEST_LATENCY_LOG` to a **different** log file per client. For
example, from PowerShell, launch the server with:

```powershell
$env:PLUTO_FASTEST_LATENCY_TRACE_CREATION = '1'
$env:PLUTO_FASTEST_LATENCY_LOG = 'C:\Starcraft\PlutoCreationTrace.server.log'
Start-Process -FilePath 'C:\Starcraft\StarCraft-SL.pluto-server.exe' -WorkingDirectory 'C:\Starcraft'
```

Then set `PLUTO_FASTEST_LATENCY_LOG` to
`C:\Starcraft\PlutoCreationTrace.client.log` and launch
`C:\Starcraft\StarCraft-SL.pluto-client.exe` the same way. Capture a room
created with the host's prior speed deliberately Slowest, and another with it
already Fastest. Record the speed displayed to **both** clients before starting
each match. Keep the clients running until the trace reaches `snet-after` or
`snet-ladder-after` and the first gameplay observation; repeat creation if possible.

Each creation emits a deferred `creation-trace:` line with a process ID, thread
ID, creation number, caller, the create-data speed byte at offset `0x26`, the
adjacent byte `0x27`, current `GameSpeed`, and the map-dialog selection byte.
The override outcome is one of `stage=override-applied` (original/final value
logged), `stage=override-noop-fastest`, or `stage=override-refused-*`. With
`PLUTO_FASTEST_LATENCY_TRACE_CREATION=1` the verbose select-map, create-data,
CreateGame/CreateLadderGame and Storm before/after stages are also logged.
`4294967295` means unavailable/not applicable, not a speed. Events are buffered
and flushed by the watcher thread (up to 2048 per client); the hooks do no file
I/O. Look for `creation-hook installed`/`creation-trace refused` and
`creation-speed: Fastest creation override armed` at startup.

After the experiment, restore your known-good DLL in both profiles before
normal play. Unset `PLUTO_FASTEST_LATENCY_TRACE_CREATION` and
`PLUTO_FASTEST_LATENCY_FORCE_CREATION_SPEED` to disable the hooks and override
on the next launch.

1. Build the x86 targets and copy `PlutoFastestLatencyFix.dll` to a stable path
   outside the game executable directory.
2. Put that DLL path in `C:\Starcraft\mods.pluto.txt` (the bot SmartLoader
   profile), using the path syntax accepted by the installed SmartLoader. For
   peer diagnostics, use `C:\Starcraft\mods.txt` with `StarCraft-SL.exe`.
3. Launch `C:\Starcraft\StarCraft-SL.pluto.exe`.
4. Confirm the DLL load in `C:\Starcraft\SmartLoader.pluto.log` and confirm
   host validation and application lines in `PlutoFastestLatencyFix.log`.

The peer's default profile uses `mods.txt` and `StarCraft-SL.exe`. The plugin
accepts any launcher executable name, but only writes to validated 1.16.1
addresses and refuses when the creation signature is not present.

With the host's prior selection Slowest, the override was verified end to end:
the host log recorded `override-applied argument=0 result=6 field26=6` before a
successful `SNetCreateLadderGame`, both clients displayed Fastest in the lobby,
the host and peer both reported `speed=6` with Fastest turn length/scheduler
`1`, and Pluto accepted four-frame latency. An already-Fastest host run (expect
`override-noop-fastest`), a confirmed full second match, and a documented
rollback run have not been recorded; keep the SmartLoader profile-file rollback
available during normal use.

The network-visible latency at `0x006556E4`, the speed-modifier table, room
settings, `bwapi.ini`, and pluto configuration are not changed.

The intentional behavioural difference from stock Fastest is a shorter local
turn period: 3 frames rather than the stock 5. Expect the bot's latency model
to remain enabled; do not expect the engine cadence to be identical to stock.

## Rollback

Remove or comment out the plugin DLL path in the active profile file
(`C:\Starcraft\mods.pluto.txt` for pluto, or `C:\Starcraft\mods.BWAPI.txt`
for BWAPI) and launch the matching stock client. Leave the built plugin files
in place if desired; no executable is overwritten. Separating or clearing old
SmartLoader and plugin logs makes it clear that the next stock launch loaded
no fix.
