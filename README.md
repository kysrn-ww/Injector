<div align="center">
   <img src="ventola.jpg" width="128" alt="ventola"></img>
   <h1>ChanchitoInjector</h1>
   <p>ManualMap DLL injector for Windows games</p>
</div>

## Supported games

| Game | Process |
|------|---------|
| CS2 | cs2.exe |
| CS:GO | csgo.exe |
| Rust | RustClient.exe |
| Garry's Mod | gmod.exe |

## How it works

Loads a DLL into a target process using **ManualMap** (manual mapping without LoadLibrary). For CS2 and CS:GO it unhooks VAC before injection and restores hooks afterwards.

Also scans for `steam_` prefixed DLLs to inject into Steam when available.

## Usage

### Auto mode
Launches injector and waits for the game to start:

```
ChanchitoInjector.exe C:\cheats\mydll.dll
```

### Manual mode
Target a specific process by name or PID:

```
ChanchitoInjector.exe cs2.exe C:\cheats\mydll.dll
ChanchitoInjector.exe 1234 C:\cheats\mydll.dll
```

## Build

1. Open `ChanchitoInjector.sln` in Visual Studio 2017+
2. Build → Build Solution (F7)
3. Binary outputs to `Release/` (x86) or `x64/Release/`

## Stack

- C++17
- Win32 API
- MSBuild / Visual Studio

> [!WARNING]
> For educational purposes only. Use at your own risk.
