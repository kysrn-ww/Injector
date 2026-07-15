<div align="center">
   <h1>ChanchitoInjector</h1>
   ManualMap DLL injector for CS2, CS:GO, Rust, and GMod
</div>

> [!CAUTION]
> Usar este injector en juegos online puede resultar en baneo.
> Úsalo bajo tu propio riesgo.

## Caracteristicas

- Inyecta DLLs en CS2, CS:GO, RustClient y GMod
- Soporta nombre de proceso y PID
- Bypass de hooks VAC para CS2/CS:GO
- Soporte para inyeccion Steam
- Interfaz de consola con salida a color

## Requisitos

- Windows
- Visual Studio 2017 o posterior

## Compilacion

1. Abrir `ChanchitoInjector.sln` en Visual Studio
2. Compilar en Debug o Release

## Uso

### Deteccion automatica de proceso

```sh
ChanchitoInjector.exe <ruta_dll>
```

### Seleccion manual de proceso

```sh
ChanchitoInjector.exe <nombre_proceso/pid> <ruta_dll>
```
