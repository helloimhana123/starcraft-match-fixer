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