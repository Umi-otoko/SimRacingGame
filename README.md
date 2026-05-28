# SimRacingGame — Hardcore Sim Racing para Steam

Arquitectura técnica de simulador de carreras comercial, estilo **Live for Speed**,
diseñado para el mercado competitivo de PC con lanzamiento en Steam.

---

## Documentación Técnica

| Documento | Descripción |
|---|---|
| [Ficha Técnica de Viabilidad](docs/TechnicalViabilitySheet.md) | Engine, stack, costos, timeline, monetización |
| [Diseño del Multiplayer Loop](docs/MultiplayerLoopDesign.md) | Netcode, CSP, reconciliación, anti-cheat frame-a-frame |

## Código Fuente

```
src/
├── Core/
│   ├── Physics/
│   │   ├── TireModel.h         ← Pacejka Magic Formula 96 (MF5.2)
│   │   └── PhysicsThread.h     ← Loop 100 Hz multi-thread + integración chasis
│   ├── Networking/
│   │   ├── NetworkTypes.h      ← Estructuras de paquetes (InputPacket, CarState, etc.)
│   │   └── ClientPrediction.h  ← Client-Side Prediction + Server Reconciliation
│   └── Setup/
│       ├── VehicleSetup.h      ← Estructuras del setup + API Save/Load
│       └── VehicleSetup.cpp    ← AES-256-CBC + HMAC-SHA256 + JSON serialization
└── Server/
    └── ServerAuthority.h       ← Loop autoritativo del servidor + anti-cheat
```

## Setup del Entorno de Desarrollo

```powershell
# Requiere: PowerShell 5.1+, ejecutar como Administrador
.\scripts\setup_environment.ps1
```

El script instala y configura:
- Visual Studio 2022 Community (C++ + Game Dev workloads)
- vcpkg + dependencias (nlohmann-json, OpenSSL, spdlog, zlib)
- JoltPhysics (clonado desde GitHub)
- Git, CMake, Python

**Pendiente manual:**
1. Epic Games Launcher + Unreal Engine 5.4+ (~80 GB)
2. Steamworks SDK (requiere cuenta en partner.steamgames.com)

## Compilar el Proyecto

```bash
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --config Release --parallel
```

## Variables de Entorno (Producción)

```
SIMRACING_HMAC_MASTER_KEY   = <128 hex chars> — Clave HMAC para setups y lap times
AWS_GAMELIFT_FLEET_ID       = <fleet-id>      — ID de fleet en AWS GameLift
STEAM_APP_ID                = <appid>         — App ID de Steam
```

## Stack Tecnológico

| Componente | Tecnología |
|---|---|
| Engine | Unreal Engine 5.4 |
| Lenguaje | C++17 |
| Física | Jolt Physics 5.x |
| Modelo de neumático | Pacejka MF5.2 |
| Red | UDP via Steamworks SDK |
| Anti-cheat | Easy Anti-Cheat (EAC) |
| Backend | AWS GameLift + DynamoDB |
| Criptografía Setups | AES-256-CBC + HMAC-SHA256 (OpenSSL) |
| Serialización | nlohmann/json |

## Referencias

- [Steamworks SDK Docs](https://partner.steamgames.com/doc/sdk)
- [Jolt Physics](https://github.com/jrouwe/JoltPhysics)
- [XVP Vehicle Physics para UE5](https://ravingbots.com/xvp-expansive-vehicle-physics-for-unreal-engine/)
- [Pacejka Magic Formula](http://www.racer.nl/reference/pacejka.htm)
- [AWS GameLift Pricing](https://aws.amazon.com/gamelift/servers/pricing/)
- [Gabriel Gambetta - Client-Side Prediction](https://www.gabrielgambetta.com/client-side-prediction-server-reconciliation.html)
