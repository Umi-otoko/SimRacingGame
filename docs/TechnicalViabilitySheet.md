# FICHA TÉCNICA DE VIABILIDAD — SIM RACING HARDCORE
## Proyecto: [Nombre Tentativo] | Steam PC | 2026-2028

---

## 1. RECOMENDACIÓN DE ENGINE: UNREAL ENGINE 5.4+

### Veredicto Final: **Unreal Engine 5.4 + C++ nativo**

Tras evaluar las tres alternativas, UE5 es la elección estratégica óptima.

---

### Análisis Comparativo de Motores

| Criterio | **UE5 (C++)** | Unity (C#) | Motor Propio (Jolt/Bullet) |
|---|---|---|---|
| **Física de vehículos** | ✅ ChaosVehicles + Jolt externo | ⚠️ PhysX parcheado, Havok adicional | ✅ Máximo control (Jolt) |
| **Rendimiento (144+ FPS)** | ✅ Nanite + Lumen + DLSS 4 | ⚠️ GC pauses en C# | ✅ Máximo (sin overhead) |
| **Shader/Render Pipeline** | ✅ Lumen GI, Nanite tessellation | ✅ HDRP | ⚠️ Todo manual |
| **Multithreading nativo** | ✅ TaskGraph System | ⚠️ Job System (mejora en 6) | ✅ Manual |
| **Royalties Steam** | ✅ 0% hasta $1M/año (luego 5%) | ✅ Sin royalties (modelo suscripción) | ✅ 0% |
| **Integración Steamworks** | ✅ Online Subsystem Steam nativo | ✅ Steamworks.NET | ⚠️ Manual total |
| **Tiempo al mercado** | ✅ 24-30 meses | ⚠️ 20-28 meses | ❌ 48-60+ meses |
| **Costo de licencia** | ✅ Gratis hasta $1M | ✅ ~$2,040/año Pro | ✅ 0% |
| **Soporte DX12 / Vulkan** | ✅ Full | ✅ Full | ⚠️ Manual |
| **Anti-Cheat (EAC/BattlEye)** | ✅ EAC integrado | ⚠️ Requiere bridge | ⚠️ Manual |

### Justificación Técnica de UE5

**Ventaja crítica #1 - Physics Threading nativo:**
El TaskGraph de UE5 permite ejecutar el Physics Tick (Chaos/Jolt) en un hilo completamente separado del Game Thread y del Render Thread. Con `FPhysScene::SetPhysXTreeRebuildRate()` y custom `FPhysicsSolver`, se logran loops de física a 100-200 Hz independientes del frame rate visual.

**Ventaja crítica #2 - Compilación Steam en 1 día:**
El Online Subsystem Steam (`OnlineSubsystemSteam`) está incluido en el engine. Configurar `DefaultEngine.ini` con `[OnlineSubsystem]` + `[OnlineSubsystemSteam]` es suficiente para tener lobbies, leaderboards y matchmaking funcionando. No hay que escribir una línea de Steamworks C++ manualmente.

**Ventaja crítica #3 - Skins DLC + Marketplace:**
El sistema de assets de UE5 permite distribuir DLCs como `.pak` files encriptados. Los skins de usuarios se cargan como `UTexture2D` dinámicos desde disco, sin recompilar el juego.

**Ventaja crítica #4 - Anti-Cheat (Easy Anti-Cheat):**
Epic Games ofrece Easy Anti-Cheat (EAC) sin costo para títulos en su plataforma. EAC tiene integración nativa con UE5 y funciona en kernel-mode en Windows, detectando modificaciones de memoria en tiempo real.

---

## 2. ARQUITECTURA GENERAL DEL SISTEMA

```
┌─────────────────────────────────────────────────────────────────┐
│                         CLIENTE (PC)                            │
│  ┌──────────────┐  ┌─────────────────┐  ┌──────────────────┐   │
│  │  Render      │  │  Game Thread    │  │  Physics Thread  │   │
│  │  Thread      │  │  (60 Hz)        │  │  (100 Hz)        │   │
│  │  (144+ FPS)  │  │                 │  │                  │   │
│  │              │  │  - Input read   │  │  - Jolt step()   │   │
│  │  - Nanite    │  │  - CSP sim      │  │  - Pacejka tires │   │
│  │  - Lumen     │  │  - Snapshot     │  │  - Suspension    │   │
│  │  - DLSS 4    │  │    interpolate  │  │  - Collision     │   │
│  └──────┬───────┘  └────────┬────────┘  └────────┬─────────┘   │
│         │                   │                    │             │
│         └───────────────────┴────────────────────┘             │
│                         ↕ Steamworks SDK                        │
└─────────────────────────────────────────────────────────────────┘
                              ↕ UDP (port 27015)
┌─────────────────────────────────────────────────────────────────┐
│                    SERVIDOR DEDICADO                             │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                Physics Authority Loop (100 Hz)           │   │
│  │  - Recibe inputs de todos los clientes                   │   │
│  │  - Simula física determinista                            │   │
│  │  - Valida posiciones/velocidades (anti-cheat server)     │   │
│  │  - Broadcast world state snapshots                       │   │
│  └────────────────────────────┬─────────────────────────────┘   │
│                               ↕                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                    Backend API Layer                      │   │
│  │  - PlayFab / AWS GameLift                                │   │
│  │  - Leaderboards anti-hack (lap times firmados)           │   │
│  │  - Inventario de skins / DLCs                            │   │
│  │  - Estadísticas de carrera (telemetria)                  │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. STACK TECNOLÓGICO COMPLETO

### Core Engine
- **Unreal Engine 5.4** — Render, input, assets, Steamworks bridge
- **Jolt Physics 5.x** — Simulación de físicas determinista multi-core
- **C++17** — Todo el gameplay code crítico (physics, netcode, crypto)

### Física de Vehículo
- **Modelo de neumático:** Pacejka Magic Formula 96 (MF5.2)
- **Dinámica de suspensión:** Double wishbone con Jolt `VehicleConstraint`
- **Motor/transmisión:** Curva de torque + diferencial LSD con Torsen-style logic
- **Física tick:** 100 Hz fijo, desacoplado del render tick

### Networking
- **Transport layer:** UDP mediante Steamworks P2P + GameServer API
- **Protocolo:** Custom protocol sobre Steamworks (no TCP — latencia inaceptable)
- **Tick rate servidor:** 64-100 Hz (configurable por sala)
- **Snapshot rate a clientes:** 30-60 Hz (delta compressed)

### Backend Cloud (Recomendado: AWS GameLift + DynamoDB)
| Servicio | Propósito | Costo estimado (300 CCU) |
|---|---|---|
| **AWS GameLift (Spot)** | Servidores de carrera dedicados | ~$80-150/mes |
| **DynamoDB** | Leaderboards, inventario | ~$10-30/mes |
| **S3** | Almacenamiento de skins de usuarios | ~$5-15/mes |
| **CloudFront CDN** | Distribución de skins | ~$10/mes |
| **Lambda** | Validación de transacciones | ~$5/mes |
| **Total estimado** | | **~$110-210/mes** |

> **Nota:** AWS GameLift Spot Instances ofrecen 50-85% de descuento vs On-Demand. Para un indie estudio, son la opción óptima con spot fleet configurado para failover automático.

### Anti-Cheat
- **Easy Anti-Cheat (EAC):** Kernel-mode, detección de memory tampering
- **Server-side validation:** Toda posición/velocidad validada en servidor
- **Replay validation:** Los tiempos de vuelta van firmados con HMAC del servidor
- **Setup validation:** Los archivos de setup están cifrados con AES-256 + HMAC-SHA256

---

## 4. MODELO DE MONETIZACIÓN

### Modelo Base: **Free to Play con DLC de Contenido**

| Tier | Contenido | Precio |
|---|---|---|
| **Base (Gratis)** | 3 coches, 2 pistas, multijugador completo | $0 |
| **Car Pack DLC** | Pack de 3 coches adicionales (modelos nuevos) | $9.99 |
| **Track Pack DLC** | Pack de 2 pistas con laser scan | $7.99 |
| **Season Pass** | Todos los DLCs del año | $24.99 |
| **Cosmético (skins premium)** | Skins para coches base (no P2W) | $1.99-4.99 |

**Regla de diseño anti-"Pay to Win":** Los DLCs de coches deben estar completamente balanceados o ser en clases separadas. Ningún cosmético altera la física. Esta regla protege la reputación en comunidades sim-racing (muy sensibles al P2W).

---

## 5. VIABILIDAD COMERCIAL

### Ruta al Mercado (Timeline)
```
Mes 1-6:   Prototype — Physics loop, car controller, 1 pista local
Mes 7-12:  Alpha — Multiplayer básico, 2 coches, leaderboards
Mes 13-18: Beta — Sistema de skins, 5 coches, Steam integration completa
Mes 19-24: Early Access Steam — Comunidad beta pública, monetización básica
Mes 25-30: Launch — Versión 1.0, marketing, GDC/PAX presence
```

### Equipo Mínimo Viable
| Rol | Descripción |
|---|---|
| CTO / Lead Engine Programmer (1) | Física, netcode, sistemas core |
| Graphics Programmer (1) | Shaders, render pipeline, efectos |
| Game Designer / Track Builder (1) | Diseño de pistas, balance de coches |
| Backend Developer (1) | AWS GameLift, API, seguridad |
| Artist / 3D Modeler (1) | Coches, pistas, UI |
| Community Manager (Part-time) | Steam forums, Discord |

### Benchmark de Referencias Comerciales (Steam)
| Juego | Lanzamiento | Engine | Pico de jugadores |
|---|---|---|---|
| Assetto Corsa Competizione | 2019 | UE4 | 12,000+ |
| BeamNG.drive | 2013 | Torque3D custom | 50,000+ |
| Automobilista 2 | 2020 | Madness Engine | 5,000+ |
| rFactor 2 | 2012 | Custom ISIMotor2 | 3,000+ |

**Conclusión:** Un título de calidad técnica demostrable puede alcanzar 5,000-15,000 pico de jugadores en Steam con marketing dirigido. El nicho sim-racing tiene alta fidelización (los jugadores invierten 500+ horas) y bajo churn.

---

## 6. REQUERIMIENTOS DE HARDWARE MÍNIMO/RECOMENDADO

| | Mínimo | Recomendado |
|---|---|---|
| **CPU** | Intel i5-8600K / Ryzen 5 3600 | Intel i7-12700K / Ryzen 7 5800X3D |
| **GPU** | RTX 2060 / RX 5700 XT | RTX 4070 / RX 7900 XT |
| **RAM** | 16 GB DDR4 | 32 GB DDR4/DDR5 |
| **Storage** | 40 GB SSD | 60 GB NVMe SSD |
| **Red** | 10 Mbps, <100ms ping | 50 Mbps, <30ms ping |
| **OS** | Windows 10 64-bit | Windows 11 64-bit |

---

## 7. RIESGOS Y MITIGACIONES

| Riesgo | Probabilidad | Impacto | Mitigación |
|---|---|---|---|
| Royalties UE5 post-$1M | Medio | Bajo | Negociar licencia enterprise si se supera |
| Cheaters en leaderboards | Alto | Crítico | EAC + server-side validation + replay system |
| Costos AWS escalan mal | Medio | Alto | Budget alerts + spot instances + auto-scaling |
| Competidor lanza antes | Medio | Medio | Early Access en mes 19 para capturar audiencia |
| Physics bugs en multiplayer | Alto | Alto | Test extensivo con Jolt determinism mode |
