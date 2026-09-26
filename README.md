# Pluto Fastest Latency Fix

This project builds a 32-bit DLL for the validated StarCraft 1.16.1 client and
a read-only diagnostic executable. The DLL is loaded by SmartLoader inside the
client; it does not patch an executable on disk and does not modify the network
latency setting.

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

## SmartLoader play workflow

1. Build the x86 targets and copy `PlutoFastestLatencyFix.dll` to a stable path
   outside the game executable directory.
2. Put that DLL path in `C:\Starcraft\mods.pluto.txt` (the pluto SmartLoader
   profile), using the path syntax accepted by the installed SmartLoader.
3. Launch `C:\Starcraft\StarCraft-SL.pluto.exe`.
4. Confirm the DLL load in `C:\Starcraft\SmartLoader.pluto.log` and confirm
   host validation and application lines in `PlutoFastestLatencyFix.log`.

The BWAPI profile uses the corresponding `mods.BWAPI.txt` and
`StarCraft-SL.BWAPI.exe` names. Do not register the plugin in both profiles
unless both clients are intentionally being patched.

The plugin supports the validated 1.16.1 address layout only. It forces local
speed index 6 (`Fastest`) and holds the local target turn length at 3, which is
the value that makes pluto observe its trained 4-frame action latency. The
network-visible latency at `0x006556E4`, room settings, `bwapi.ini`, and pluto
configuration are not changed.

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