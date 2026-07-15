# ChanchitoInjector

Inyector de DLL por ManualMap para juegos en Windows.

## Juegos compatibles

| Juego | Proceso |
|-------|---------|
| CS2 | cs2.exe |
| CS:GO | csgo.exe |
| Rust | RustClient.exe |
| Garry's Mod | gmod.exe |

## Como funciona

Toma una DLL y la inyecta en un proceso destino usando **ManualMap** (mapeo manual sin LoadLibrary). Para CS2 y CS:GO desactiva los hooks de VAC antes de inyectar y los restaura al terminar.

Tambien busca DLLs con prefijo `steam_` para inyectarlas en Steam si existen.

## Modos de uso

### Automatico
Ejecuta el inyector y espera a que el juego arranque:

```
ChanchitoInjector.exe C:\cheats\mydll.dll
```

### Manual
Especifica proceso o PID directamente:

```
ChanchitoInjector.exe cs2.exe C:\cheats\mydll.dll
ChanchitoInjector.exe 1234 C:\cheats\mydll.dll
```

## Compilar

1. Abri `ChanchitoInjector.sln` con Visual Studio 2017+
2. Build → Build Solution (F7)
3. El .exe sale en `Release/` o `x64/Release/`

## Stack

- C++17
- Win32 API
- MSBuild / Visual Studio

> [!WARNING]
> El autor no se responsabiliza por baneos. Solo para uso educativo.
