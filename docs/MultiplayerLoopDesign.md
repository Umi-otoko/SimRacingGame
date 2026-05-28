# DISEÑO DEL MULTIPLAYER LOOP — SIM RACING
## Arquitectura Client-Server: Validación Frame-a-Frame

---

## 1. PRINCIPIOS FUNDAMENTALES

### Autoridad del Servidor (Server-Authoritative Physics)
El servidor es la **única fuente de verdad**. Los clientes predicen su propio estado localmente para eliminar la percepción de lag, pero el servidor valida y corrige cada posición/velocidad que diverge.

```
NUNCA confiamos en el cliente para:
  - Su posición/velocidad final
  - Los tiempos de vuelta
  - El estado de colisiones
  - El inventario/skins

SÍ confiamos en el cliente para:
  - Sus inputs (throttle, freno, dirección, marchas)
  - La predicción local de su propio coche (solo visual)
```

---

## 2. DIAGRAMA DEL LOOP POR FRAME

### Arquitectura de Ticks Desacoplados

```
CLIENTE:
┌─────────────────────────────────────────────────────────────────┐
│  Render Thread (144 Hz — variable)                              │
│  ├─ Interpola posición del coche local (entre physics snapshots)│
│  ├─ Interpola coches fantasma (ghost interpolation)             │
│  └─ Rendering: Nanite, Lumen, VFX, UI                          │
│                                                                  │
│  Game Thread (60 Hz — fijo)                                     │
│  ├─ Lee inputs del jugador (gampad/wheel/teclado)               │
│  ├─ Asigna InputSequenceNumber++ al input                       │
│  ├─ Guarda input en el Input History Buffer                     │
│  ├─ Envía InputPacket al servidor (UDP)                         │
│  ├─ Aplica input a la simulación local (Client-Side Prediction) │
│  └─ Procesa StateSnapshot del servidor (Reconciliation)         │
│                                                                  │
│  Physics Thread (100 Hz — fijo)                                 │
│  ├─ Jolt Physics World Step (dt = 0.01s)                        │
│  ├─ Pacejka tire forces por rueda                               │
│  ├─ Suspension spring/damper                                    │
│  └─ Actualiza transformada del coche                            │
└─────────────────────────────────────────────────────────────────┘

SERVIDOR DEDICADO:
┌─────────────────────────────────────────────────────────────────┐
│  Authority Physics Loop (100 Hz — fijo)                         │
│  ├─ Recopila inputs de TODOS los clientes para el tick actual   │
│  ├─ Si falta input de un cliente: aplica el último recibido     │
│  ├─ Simula Jolt Physics World con TODOS los coches              │
│  ├─ Valida estado resultante (anti-cheat)                       │
│  ├─ Genera StateSnapshot firmado                                │
│  └─ Broadcast snapshot a clientes (30-60 Hz)                    │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. FLUJO DE COMUNICACIÓN FRAME A FRAME

### Paso 1: El Cliente Captura y Envía Input

```
Frame N (t = 100ms):
─────────────────────────────────────────────────────────────────
CLIENTE:
  input = {
    sequence:    1042,           // ID único, monotónico, incrementa siempre
    timestamp:   100.000ms,      // Tiempo local del cliente
    throttle:    0.85f,          // [0.0, 1.0]
    brake:       0.00f,          // [0.0, 1.0]
    steering:    -0.23f,         // [-1.0, 1.0]
    gear:        4,              // [0=N, 1-7=marchas, -1=R]
    clutch:      0.00f,          // [0.0, 1.0] (si tiene H-shifter)
    handbrake:   false,
    checksum:    CRC32(data)     // Detección de corrupción de paquete
  }

  // Aplica input inmediatamente (Client-Side Prediction)
  physics_thread.ApplyInput(input);  // No espera confirmación del servidor

  // Guarda en buffer para reconciliación posterior
  input_history.push_back(input);    // Buffer circular de ~256 entradas

  // Envía al servidor (UDP, sin garantía de entrega — < 50 bytes)
  network.SendUnreliable(SERVER, input);
─────────────────────────────────────────────────────────────────
```

### Paso 2: El Servidor Recibe Inputs y Simula

```
Servidor en t = 100ms + RTT/2:
─────────────────────────────────────────────────────────────────
  // Recopila todos los inputs en la ventana de tiempo del tick
  inputs_this_tick = input_queue.Drain();

  // Para cada coche en la carrera:
  for each Car c in race:
    if inputs_this_tick.HasInput(c.player_id):
      c.pending_input = inputs_this_tick[c.player_id];
    else:
      // Input perdido: reusa último (mejor que simular sin input)
      c.pending_input = c.last_known_input;  // Lag compensation

  // Paso de física determinista
  jolt_world.Step(dt=0.01f);  // 100 Hz fijo — MISMO dt que el cliente

  // === VALIDACIÓN ANTI-CHEAT ===
  for each Car c in race:
    ValidateCarState(c);  // Ver sección 5

  // Genera snapshot del mundo
  snapshot = {
    tick:            world_tick++,           // uint64, jamás retrocede
    timestamp_ms:    GetServerTime(),
    cars: [
      {
        player_id:    c.player_id,
        position:     c.position,            // vec3, float64 (precisión)
        velocity:     c.velocity,            // vec3
        rotation:     c.rotation,            // quaternion
        angular_vel:  c.angular_velocity,    // vec3
        input_ack:    c.last_input_seq,      // Último input procesado
      }
      for c in race.cars
    ],
    hmac: HMAC_SHA256(snapshot_data, server_secret_key)  // Firma del servidor
  }

  // Envía snapshot (delta-compressed) a todos los clientes
  for each Client in race:
    SendDeltaSnapshot(Client, snapshot, Client.last_acked_snapshot);
─────────────────────────────────────────────────────────────────
```

### Paso 3: El Cliente Reconcilia (Server Reconciliation)

```
CLIENTE recibe snapshot del servidor:
─────────────────────────────────────────────────────────────────
  // El servidor me dice que en el tick T procesó mi input secuencia S
  last_acked_seq = snapshot.cars[my_id].input_ack;  // Ej: secuencia 1038

  // Mi estado autoritativo según el servidor
  authoritative_state = snapshot.cars[my_id];  // Posición/velocidad oficial

  // Calcula error entre mi predicción local y el estado del servidor
  prediction_error = Distance(my_predicted_position, authoritative_state.position);

  if prediction_error > RECONCILE_THRESHOLD (0.05m):
    // Corrección necesaria:

    // 1. Retroceder al estado autoritativo del servidor
    physics.SetState(authoritative_state);

    // 2. Re-aplicar todos los inputs desde last_acked_seq hasta ahora
    for each input in input_history where input.sequence > last_acked_seq:
      physics.ApplyInputAndStep(input, dt=0.01f);  // Re-simulación rápida

    // 3. El resultado es mi posición corregida, suavizada visualmente
    visual_position = SmoothLerp(visual_position, physics.position, 0.3f);
  else:
    // Error mínimo: sin corrección visible, descarta silenciosamente
    // Actualiza el estado de referencia
    reference_state = authoritative_state;
─────────────────────────────────────────────────────────────────
```

---

## 4. INTERPOLACIÓN DE COCHES FANTASMA (Otros Jugadores)

Los coches de otros jugadores **nunca usan Client-Side Prediction** (no tenemos sus inputs). Se interpolan suavemente entre snapshots del servidor.

```
// Para cada coche que NO es el jugador local:
GhostCar.Update(float render_dt):
  // Buffer de snapshots del servidor (almacena últimos 10 snapshots)
  // Siempre renderizamos ~100ms ATRÁS para tener 2 snapshots entre los
  // cuales interpolar sin saltos

  render_time = server_time - INTERP_DELAY_MS (100ms);

  // Encontrar los dos snapshots más cercanos al render_time
  snap_before = snapshots.FindBefore(render_time);
  snap_after  = snapshots.FindAfter(render_time);

  if snap_before AND snap_after:
    // Interpolación lineal para posición
    t = (render_time - snap_before.timestamp) /
        (snap_after.timestamp - snap_before.timestamp);

    position = Lerp(snap_before.position, snap_after.position, t);
    rotation = Slerp(snap_before.rotation, snap_after.rotation, t);

    // Extrapolación (dead reckoning) si el snapshot más reciente
    // es demasiado viejo (>200ms):
    if TimeSinceLastSnapshot > 200ms:
      position += snap_after.velocity * dt_since_last_snap;

  ghost_mesh.SetTransform(position, rotation);
```

---

## 5. VALIDACIÓN ANTI-CHEAT EN SERVIDOR

El servidor calcula los límites físicos máximos posibles para cada coche y los compara contra el estado reportado/simulado.

```
ValidateCarState(Car c):
  // 1. Velocidad máxima física
  max_speed_mps = car.engine.max_torque * car.gear.ratio /
                  (car.tire.rolling_radius * car.mass * DRAG_FACTOR);
  max_speed_mps = min(max_speed_mps, car.aerodynamics.terminal_velocity);

  if Length(c.velocity) > max_speed_mps * 1.10:  // 10% tolerancia
    LogCheatAttempt(c, "SPEED_VIOLATION", Length(c.velocity), max_speed_mps);
    c.velocity = Normalize(c.velocity) * max_speed_mps;  // Corregir
    return false;

  // 2. Teleportación (distancia máxima entre ticks)
  max_displacement = max_speed_mps * PHYSICS_DT * 1.5;  // 50% margen
  actual_displacement = Length(c.position - c.prev_position);

  if actual_displacement > max_displacement:
    LogCheatAttempt(c, "TELEPORT_VIOLATION", actual_displacement, max_displacement);
    c.position = c.prev_position + Normalize(c.position - c.prev_position) * max_displacement;
    return false;

  // 3. Aceleración lateral (curvas imposibles)
  lateral_accel = Dot(c.acceleration, c.right_vector);
  max_lateral_g = car.tire.max_lateral_force / (c.mass * 9.81);

  if abs(lateral_accel) > max_lateral_g * 9.81 * 1.20:  // 20% tolerancia
    LogCheatAttempt(c, "LATERAL_G_VIOLATION", lateral_accel, max_lateral_g);
    return false;

  // 4. Input rate flooding (flood de inputs para simular más rápido)
  inputs_this_sec = c.input_counter_this_second;
  if inputs_this_sec > MAX_INPUTS_PER_SECOND * 1.5:
    LogCheatAttempt(c, "INPUT_FLOOD", inputs_this_sec, MAX_INPUTS_PER_SECOND);
    return false;

  return true;  // Estado válido
```

---

## 6. VALIDACIÓN DE TIEMPOS DE VUELTA

Los tiempos de vuelta son el dato más sensible. Se generan en el servidor con firma criptográfica.

```
// El servidor genera el lap time cuando el coche cruza la línea de meta
LapTimeRecord Server.ValidateLapCross(Car c, Checkpoint finish_line):
  lap_time_ms = current_tick_time - c.lap_start_time;

  // Verificar que el coche pasó por TODOS los checkpoints del circuito
  if not c.checkpoint_history.ContainsAll(track.required_checkpoints):
    LogCheatAttempt(c, "CHECKPOINT_SKIP");
    return null;  // Vuelta inválida

  // Verificar que la velocidad en la meta es físicamente posible
  if Length(c.velocity) < FINISH_LINE_MIN_SPEED:
    LogCheatAttempt(c, "IMPOSSIBLE_FINISH_SPEED");
    return null;

  // Construir y firmar el record
  record = {
    player_id:    c.player_id,
    track_id:     track.id,
    car_id:       c.car_id,
    lap_time_ms:  lap_time_ms,
    server_tick:  current_tick,
    timestamp:    DateTime.UtcNow,
    replay_hash:  SHA256(c.replay_data),  // Hash del replay completo
  }
  record.signature = HMAC_SHA256(record, SERVER_LEADERBOARD_SECRET);

  // Solo records firmados por el servidor son aceptados en el leaderboard
  leaderboard_service.SubmitLapTime(record);
  return record;
```

---

## 7. LAG COMPENSATION PARA COLISIONES

Cuando un jugador reporta una colisión con otro coche, el servidor retrocede el tiempo para validar que esa colisión era posible dado el ping del jugador.

```
// Cuando el servidor recibe una colisión reportada por cliente A:
ServerHandleCollision(Client A, CollisionEvent event):
  // Retroceder el mundo al tiempo T - (A.ping / 2) ms
  // El servidor mantiene un historial de estados de 500ms
  lag_ms = A.average_rtt / 2;

  if lag_ms > MAX_LAG_COMPENSATION_MS (200ms):
    // Ping demasiado alto — no hacemos lag comp, usamos estado actual
    lag_ms = 0;

  rewind_state = state_history.GetStateAt(CurrentTime - lag_ms);

  car_A_then = rewind_state.GetCar(A.player_id);
  car_B_then = rewind_state.GetCar(event.other_car_id);

  // Verificar si la colisión era geométricamente posible
  distance_then = Length(car_A_then.position - car_B_then.position);
  collision_threshold = car_A_then.bounding_radius + car_B_then.bounding_radius;

  if distance_then <= collision_threshold * 1.5:
    // Colisión válida — aplicar respuesta de colisión
    ApplyCollisionResponse(A, event.other_car_id, event.impact_normal, event.impact_velocity);
  else:
    // Colisión imposible — potencial hack
    LogSuspiciousActivity(A, "IMPOSSIBLE_COLLISION");
```

---

## 8. ESTRUCTURA DE PAQUETES DE RED

### InputPacket (Cliente → Servidor) — ~48 bytes
```
struct InputPacket {
    uint64_t sequence_number;    // 8 bytes - ID único del input
    uint64_t client_timestamp;   // 8 bytes - Timestamp del cliente (ms)
    float    throttle;           // 4 bytes - [0.0, 1.0]
    float    brake;              // 4 bytes - [0.0, 1.0]
    float    steering;           // 4 bytes - [-1.0, 1.0]
    float    clutch;             // 4 bytes - [0.0, 1.0]
    int8_t   gear;               // 1 byte  - [-1, 7]
    bool     handbrake;          // 1 byte
    uint8_t  flags;              // 1 byte  - flip reset, pit request, etc.
    uint8_t  _padding[3];        // 3 bytes - alineación
    uint32_t checksum;           // 4 bytes - CRC32 del paquete
    // TOTAL: 48 bytes
};
```

### StateSnapshot (Servidor → Clientes) — Variable (~80 bytes por coche)
```
struct CarSnapshot {
    uint32_t player_id;          // 4 bytes
    uint64_t input_ack;          // 8 bytes - Último input procesado
    float    position[3];        // 12 bytes - (x, y, z) float64 si necesario
    float    velocity[3];        // 12 bytes
    float    rotation[4];        // 16 bytes - Quaternion (w, x, y, z)
    float    angular_velocity[3];// 12 bytes
    float    wheel_speeds[4];    // 16 bytes - rad/s por rueda
    uint8_t  gear;               // 1 byte
    uint8_t  damage_flags;       // 1 byte  - bits: FL/FR/RL/RR/engine/etc.
    uint16_t rpm;                // 2 bytes
    // TOTAL: 84 bytes por coche
};

struct WorldSnapshot {
    uint64_t server_tick;        // 8 bytes
    uint32_t race_time_ms;       // 4 bytes
    uint8_t  num_cars;           // 1 byte
    CarSnapshot cars[];          // num_cars * 84 bytes
    uint8_t  hmac[32];           // 32 bytes - HMAC-SHA256 de todo el snapshot
    // TOTAL (10 coches): ~893 bytes → delta-compress a ~200-300 bytes
};
```

> **Nota sobre delta compression:** Solo se envían los campos que cambiaron desde el último snapshot confirmado. A 200 km/h, posición siempre cambia; `gear` y `damage_flags` raramente cambian, ahorrando ~30% del ancho de banda.
