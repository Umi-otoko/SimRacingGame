# SimRacingGame — Checklist de desarrollo

> Nuestro LFS. Marcamos cada ítem cuando está en el repo.

---

## FASE 0 — Base estable
- [x] 0.1  C++ vehicle pawn + ChaosVehicles (`RacingVehiclePawn`)
- [x] 0.2  Modelo Pacejka MF5.2 en paralelo (`TireModel.h`)
- [x] 0.3  Enhanced Input System (WASD + gamepad)
- [x] 0.4  HUD canvas C++: velocidad, marcha, RPM, temps, G-force, countdown
- [x] 0.5  GameMode con countdown 3-2-1-GO, replicado al cliente
- [x] 0.6  GameState replica fase de carrera
- [x] 0.7  Python script reconstruye el nivel sin crashes
- [x] 0.8  Circuito oval ~1.3 km con barreras (Python)

## FASE 1 — Sonido sin assets externos ✅
- [x] 1.1  `URacingAudio : USynthComponent` — síntesis FM procedural
- [x] 1.2  Armónicos de motor (4 cil) respondiendo al RPM
- [x] 1.3  Volumen sube con el acelerador
- [x] 1.4  Chirrido de neumáticos (ruido filtrado × slip ratio)
- [x] 1.5  Wired en `RacingVehiclePawn`: SetRPM / SetThrottle / SetTireSlip cada tick

## FASE 2 — Sistema de vuelta completo ✅
- [x] 2.1  `ARacingCheckpoint : ATriggerBox` — registra paso al GameMode
- [x] 2.2  `ARacingFinishLine : ATriggerBox` — dispara validación de vuelta
- [x] 2.3  GameMode auto-cuenta checkpoints al BeginPlay
- [x] 2.4  Python coloca 4 checkpoints + finish line en el circuito
- [x] 2.5  HUD muestra lap actual, mejor vuelta y timer de carrera

## FASE 3 — Efectos visuales de neumáticos
- [ ] 3.1  Niagara humo de neumáticos (activo cuando slip > threshold)
- [ ] 3.2  Skid marks con `UDecalComponent` (material de asfalto quemado)
- [ ] 3.3  Chispas en impacto con barrera
- [ ] 3.4  Pantalla de resultados post-carrera (Widget Blueprint)

## FASE 4 — Física avanzada
- [ ] 4.1  Pacejka conectado al solver de Chaos (override de fuerzas)
- [ ] 4.2  `FrictionForceMultiplier` dinámico por temperatura de neumático
- [ ] 4.3  Downforce aerodinámico (crece con v²)
- [ ] 4.4  Modelo de transferencia de peso (anti-roll bar virtual)

## FASE 5 — Pista y mundo
- [ ] 5.1  Geometría con curvas y cambios de elevación
- [ ] 5.2  Material de asfalto + hierba (Megascans o procedural)
- [ ] 5.3  Cielo y niebla volumétrica (Exponential Height Fog)
- [ ] 5.4  Gradas y pit lane con props básicos

## FASE 6 — Multijugador
- [ ] 6.1  Session creation via Steam (Lobby)
- [ ] 6.2  Spawns por posición en grilla
- [ ] 6.3  Leaderboard AWS DynamoDB + firma HMAC
- [ ] 6.4  Client-side prediction completo (snapshot + reconciliar)

---

*Última actualización automática por el build.*
