# Guía de Setup del Entorno de Desarrollo

## Estado de las Herramientas (verificado 2026-05-27)

| Herramienta | Versión | Estado |
|---|---|---|
| Git | 2.54.0 | ✅ Instalado |
| CMake | 4.3.2 | ✅ Instalado |
| Python | 3.14.4 | ✅ Instalado |
| GitHub CLI (gh) | 2.93.0 | ✅ Instalado |
| OpenSSL (sistema) | 3.5.6 | ✅ Instalado |
| VS2022 Build Tools | MSVC 19.44 / 14.44.35207 | ✅ Instalado |
| vcpkg | 2026-04-08 | ✅ Instalado en C:\vcpkg |
| JoltPhysics | latest (depth=1) | ✅ en extern/JoltPhysics |
| nlohmann-json | 3.12.0 | ✅ via vcpkg |
| spdlog | 1.17.0 | ✅ via vcpkg |
| zlib | 1.3.2 | ✅ via vcpkg |
| gtest | 1.17.0 | ✅ via vcpkg |
| openssl (vcpkg) | — | ⏳ Instalando |

## Pendiente (requiere acción manual)

| Herramienta | Motivo | Enlace |
|---|---|---|
| Epic Games Launcher + UE 5.4 | ~80 GB, requiere cuenta Epic | https://www.unrealengine.com/download |
| Steamworks SDK | Requiere cuenta de Steamworks Partner | https://partner.steamgames.com/doc/sdk |

## Compilar el Proyecto

```powershell
# Desde la raíz del proyecto
cmake -B build `
  -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_BUILD_TYPE=Release `
  -G "Visual Studio 17 2022" -A x64

cmake --build build --config Release --parallel
```

## Variables de Entorno de Desarrollo

```powershell
# Agregar a tu perfil de PowerShell ($PROFILE)
$env:SIMRACING_ROOT       = "C:\Users\amdry\SimRacingGame"
$env:JOLT_PHYSICS_DIR     = "C:\Users\amdry\SimRacingGame\extern\JoltPhysics"
$env:STEAMWORKS_SDK_DIR   = "C:\Users\amdry\SimRacingGame\extern\steamworks_sdk"
$env:VCPKG_ROOT           = "C:\vcpkg"
# Para produccion (NO en desarrollo):
# $env:SIMRACING_HMAC_MASTER_KEY = "<128 hex chars>"
```

## Estructura del Proyecto

```
SimRacingGame/
├── CMakeLists.txt              ← Build system principal
├── .gitignore
├── docs/
│   ├── TechnicalViabilitySheet.md   ← Ficha técnica CTO
│   ├── MultiplayerLoopDesign.md     ← Netcode frame-a-frame
│   └── DevSetup.md                  ← Esta guía
├── src/
│   ├── Core/
│   │   ├── Physics/
│   │   │   ├── TireModel.h      ← Pacejka MF5.2
│   │   │   └── PhysicsThread.h  ← 100 Hz multi-thread
│   │   ├── Networking/
│   │   │   ├── NetworkTypes.h      ← Paquetes UDP
│   │   │   └── ClientPrediction.h  ← CSP + Reconciliación
│   │   └── Setup/
│   │       ├── VehicleSetup.h   ← API pública
│   │       └── VehicleSetup.cpp ← AES-256 + HMAC-SHA256
│   └── Server/
│       └── ServerAuthority.h   ← Loop autoritativo
├── scripts/
│   └── setup_environment.ps1   ← Instalador automático
├── extern/
│   ├── JoltPhysics/             ← Motor de físicas (gitignored)
│   └── steamworks_sdk/          ← SDK de Steam (gitignored)
└── assets/
    ├── shaders/
    └── skins/
```
