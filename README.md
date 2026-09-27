# Pluto Fastest Latency Fix

This project builds a 32-bit DLL for the validated StarCraft 1.16.1 client and
a read-only diagnostic executable. The DLL is loaded by SmartLoader inside the
client; it does not patch an executable on disk and does not modify the network
latency setting. It is currently diagnostic-only while a safe
pre-initialization correction mechanism is investigated.

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

### Experimental pre-lobby creation trace

An opt-in diagnostic build records the host's creation path without forcing game
speed. Build the x86 Release DLL and install it in the existing server and client
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

`creation-trace` records select-map, creation-data before/after, CreateGame or
CreateLadderGame and the corresponding Storm create call before/after, with a
process ID, thread ID, creation
number, caller, candidate byte at create-data offset `0x26`, adjacent byte
`0x27`, current `GameSpeed`, and the map-dialog selection byte. `4294967295`
means unavailable/not applicable, not a speed. Events are buffered and flushed
by the watcher thread (up to 2048 per client); the hooks do no file I/O. Look
for `creation-trace installed` or `creation-trace refused` at startup. The
trace does not establish that offset `0x26` is authoritative until host and
peer lobby and gameplay observations agree with the captured values.

After the experiment, restore your known-good DLL in both profiles before
normal play. Unset `PLUTO_FASTEST_LATENCY_TRACE_CREATION` to disable the
diagnostic hooks on the next launch.

1. Build the x86 targets and copy `PlutoFastestLatencyFix.dll` to a stable path
   outside the game executable directory.
2. Put that DLL path in `C:\Starcraft\mods.pluto.txt` (the bot SmartLoader
   profile), using the path syntax accepted by the installed SmartLoader. For
   peer diagnostics, use `C:\Starcraft\mods.txt` with `StarCraft-SL.exe`.
3. Launch `C:\Starcraft\StarCraft-SL.pluto.exe`.
4. Confirm the DLL load in `C:\Starcraft\SmartLoader.pluto.log` and confirm
   host validation and application lines in `PlutoFastestLatencyFix.log`.

The peer's default profile uses `mods.txt` and `StarCraft-SL.exe`. The plugin
accepts any launcher executable name, but only reads validated 1.16.1 memory
state. Do not enable experimental correction writes: initialized scheduler
writes produced local 4-frame samples but failed two-client integrity tests.

The current derivation-time correction has been verified through startup and
Pluto's latency probe on both clients: both derived Fastest turn length `1` and
Pluto accepted four-frame latency. A complete match through to its natural end,
broader regression checks, and a documented rollback run have not been recorded;
keep the SmartLoader profile-file rollback available during normal use.

The plugin accepts any SmartLoader launcher executable name, but supports only
the validated 1.16.1 memory signature and refuses writes when that signature is
not present. It currently records live timing state without forcing speed or
turn length. The network-visible latency at `0x006556E4`, room settings,
`bwapi.ini`, and pluto configuration are not changed.

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
