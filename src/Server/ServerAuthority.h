#pragma once
// =============================================================================
// ServerAuthority.h
// Loop autoritativo del servidor: simula toda la carrera, valida anti-cheat,
// genera snapshots firmados y administra tiempos de vuelta.
//
// Corre como proceso separado (Dedicated Server) en AWS GameLift.
// =============================================================================

#include "../Core/Networking/NetworkTypes.h"
#include "../Core/Physics/PhysicsThread.h"
#include <unordered_map>
#include <vector>
#include <deque>
#include <functional>
#include <cmath>
#include <array>
#include <cstring>
#include <chrono>

namespace SimRacing {

static constexpr int    SERVER_TICK_HZ          = 100;
static constexpr float  SERVER_DT               = 1.0f / SERVER_TICK_HZ;
static constexpr float  MAX_SPEED_SANITY_MPS    = 100.0f;  // ~360 km/h absoluto
static constexpr float  MAX_LATERAL_G           = 5.5f;    // g (GT3 máximo real)
static constexpr int    MAX_INPUTS_PER_SECOND   = 150;     // 100 Hz + 50% margen
static constexpr double LAG_COMP_MAX_MS         = 200.0;
static constexpr int    STATE_HISTORY_TICKS      = 50;     // 0.5 seg a 100 Hz

// =============================================================================
// ServerCarState — Estado completo de un coche en el servidor
// =============================================================================

struct ServerCarState {
    uint32_t player_id       = 0;
    bool     is_connected    = false;

    VehiclePhysicsState physics; // Estado autoritativo de físicas

    // Anti-cheat
    PlayerInput  last_input;
    uint64_t     last_input_seq      = 0;
    uint32_t     inputs_this_second  = 0;
    uint32_t     cheat_strike_count  = 0;
    bool         is_banned           = false;

    // Carrera
    int      current_lap         = 1;
    int      total_laps          = 0;
    uint64_t lap_start_tick      = 0;
    uint64_t best_lap_ms         = UINT64_MAX;
    std::vector<bool> checkpoints_hit;
    float    race_distance_m     = 0.0f;

    // Historial de estados para lag compensation
    std::deque<VehiclePhysicsState> state_history;
};

// =============================================================================
// CheatEvent — Registro de intentos de trampa para análisis posterior
// =============================================================================

enum class CheatType : uint8_t {
    SPEED_VIOLATION,
    TELEPORT_VIOLATION,
    LATERAL_G_VIOLATION,
    INPUT_FLOOD,
    CHECKPOINT_SKIP,
    IMPOSSIBLE_COLLISION,
    LAP_TIME_INVALID,
};

struct CheatEvent {
    uint32_t  player_id;
    CheatType type;
    float     measured_value;
    float     max_allowed_value;
    uint64_t  server_tick;
};

// =============================================================================
// ServerAuthorityLoop — Motor principal del servidor dedicado
// =============================================================================

class ServerAuthorityLoop {
public:
    using SnapshotCallback = std::function<
        void(const Net::WorldSnapshotHeader&, const std::vector<Net::CarState>&)>;
    using CheatCallback    = std::function<void(const CheatEvent&)>;
    using LapTimeCallback  = std::function<void(const Net::LapTimeRecord&)>;

    explicit ServerAuthorityLoop(std::string_view hmac_key_hex)
        : current_tick_(0), race_started_(false)
    {
        // Cargar clave HMAC del servidor desde config
        LoadHMACKey(hmac_key_hex);
    }

    // -------------------------------------------------------------------------
    // ReceiveInput — Thread-safe. Llamado por el Network Thread al recibir un paquete
    // -------------------------------------------------------------------------
    void ReceiveInput(const Net::InputPacket& packet, uint32_t player_id) {
        if (!packet.VerifyCRC()) return;  // Paquete corrupto → descartar

        std::lock_guard<std::mutex> lock(input_queue_mutex_);

        auto& queue = pending_inputs_[player_id];
        // Descartar inputs out-of-order (secuencia más vieja que la última procesada)
        if (!queue.empty() && packet.sequence <= queue.back().sequence) return;

        queue.push_back(packet);
        if (queue.size() > 10) queue.pop_front();  // Limitar buffer por jugador
    }

    // -------------------------------------------------------------------------
    // Tick — Llamado exactamente a 100 Hz por el timer del servidor
    // -------------------------------------------------------------------------
    void Tick() {
        ++current_tick_;

        // 1. Recopilar inputs de todos los jugadores para este tick
        CollectInputs();

        // 2. Simular física de todos los coches
        SimulatePhysics();

        // 3. Validar anti-cheat
        ValidateAllCars();

        // 4. Detectar cruce de líneas de meta y checkpoints
        ProcessTrackEvents();

        // 5. Guardar estado en historial (para lag compensation)
        SaveStateHistory();

        // 6. Emitir snapshot a todos los clientes (cada N ticks según tick rate cliente)
        if (current_tick_ % 2 == 0) {  // 50 Hz a los clientes
            EmitWorldSnapshot();
        }
    }

private:
    uint64_t current_tick_;
    bool     race_started_;

    std::unordered_map<uint32_t, ServerCarState>            cars_;
    std::unordered_map<uint32_t, std::deque<Net::InputPacket>> pending_inputs_;
    std::mutex input_queue_mutex_;

    std::array<uint8_t, 64> hmac_key_;

    SnapshotCallback on_snapshot_;
    CheatCallback    on_cheat_;
    LapTimeCallback  on_lap_time_;

    // -------------------------------------------------------------------------
    void CollectInputs() {
        std::lock_guard<std::mutex> lock(input_queue_mutex_);

        for (auto& [pid, car] : cars_) {
            auto it = pending_inputs_.find(pid);
            if (it == pending_inputs_.end() || it->second.empty()) {
                // Sin input: reusar el último (mejor que simular sin input)
                continue;
            }

            // Tomar el último input recibido para este tick
            const Net::InputPacket& pkt = it->second.back();
            it->second.clear();

            // Verificar flood de inputs (anti-cheat)
            car.inputs_this_second++;
            if (car.inputs_this_second > MAX_INPUTS_PER_SECOND) {
                RecordCheat(car, CheatType::INPUT_FLOOD,
                            static_cast<float>(car.inputs_this_second),
                            static_cast<float>(MAX_INPUTS_PER_SECOND));
                continue;
            }

            // Convertir paquete de red a PlayerInput
            PlayerInput input;
            input.sequence_number = pkt.sequence;
            input.throttle        = pkt.throttle;
            input.brake           = pkt.brake;
            input.steering        = pkt.steering;
            input.clutch          = pkt.clutch;
            input.gear            = pkt.gear;
            input.handbrake       = (pkt.flags & 0x01) != 0;

            car.last_input     = input;
            car.last_input_seq = pkt.sequence;
        }

        // Resetear contador de inputs por segundo cada segundo
        if (current_tick_ % SERVER_TICK_HZ == 0) {
            for (auto& [pid, car] : cars_)
                car.inputs_this_second = 0;
        }
    }

    // -------------------------------------------------------------------------
    void SimulatePhysics() {
        // En producción: Jolt Physics World Step con todos los coches como RigidBodies
        // Aquí: actualización simplificada para demostración de la arquitectura
        for (auto& [pid, car] : cars_) {
            if (!car.is_connected || car.is_banned) continue;

            const PlayerInput& input = car.last_input;
            auto& state = car.physics;

            // Aplicar throttle/brake de forma simple
            const float drive_force = input.throttle * 12000.0f;  // N
            const float brake_force = input.brake    * 18000.0f;  // N

            const float speed = state.velocity.Length();
            const float net_force = drive_force - brake_force
                                    - (speed * speed * 0.45f);  // drag

            const float mass = 1350.0f;  // kg — GT3 típico
            const float accel = net_force / mass;

            // Integrar (dirección simplificada — Jolt maneja la realidad)
            state.velocity.z += accel * SERVER_DT;
            state.position   = state.position + state.velocity * SERVER_DT;
            state.speed_kmh  = speed * 3.6f;
            state.tick        = current_tick_;
        }
    }

    // -------------------------------------------------------------------------
    void ValidateAllCars() {
        for (auto& [pid, car] : cars_) {
            if (!car.is_connected || car.is_banned) continue;

            const auto& state = car.physics;
            const float speed = state.velocity.Length();

            // Validación 1: Velocidad máxima absoluta
            if (speed > MAX_SPEED_SANITY_MPS * 1.10f) {
                RecordCheat(car, CheatType::SPEED_VIOLATION,
                            speed, MAX_SPEED_SANITY_MPS);
                // Corrección autoritativa: limitar velocidad
                const float scale = MAX_SPEED_SANITY_MPS / speed;
                car.physics.velocity.x *= scale;
                car.physics.velocity.y *= scale;
                car.physics.velocity.z *= scale;
            }

            // Validación 2: Teleportación
            if (car.state_history.size() >= 2) {
                const auto& prev = car.state_history.back();
                const float dx = state.position.x - prev.position.x;
                const float dy = state.position.y - prev.position.y;
                const float dz = state.position.z - prev.position.z;
                const float displacement = std::sqrt(dx*dx + dy*dy + dz*dz);
                const float max_disp = speed * SERVER_DT * 1.5f + 0.1f;  // 10cm slack

                if (displacement > max_disp && max_disp > 0.01f) {
                    RecordCheat(car, CheatType::TELEPORT_VIOLATION,
                                displacement, max_disp);
                    // Revertir posición
                    car.physics.position = prev.position;
                    car.physics.velocity = prev.velocity;
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    void ProcessTrackEvents() {
        // En producción: verificar colisiones con triggers de checkpoint/meta
        // Los checkpoints son volúmenes AABBs o planos definidos en el track data
        for (auto& [pid, car] : cars_) {
            if (!car.is_connected) continue;

            // Pseudo-código: verificar si el coche cruzó la meta
            // if (finish_line_trigger.Contains(car.physics.position)) {
            //     TryValidateLapTime(car);
            // }
        }
    }

    // -------------------------------------------------------------------------
    void TryValidateLapTime(ServerCarState& car) {
        // Verificar que todos los checkpoints fueron cruzados
        for (bool hit : car.checkpoints_hit) {
            if (!hit) {
                RecordCheat(car, CheatType::CHECKPOINT_SKIP, 0.0f, 1.0f);
                ResetCheckpoints(car);
                return;
            }
        }

        uint64_t lap_time_ms = (current_tick_ - car.lap_start_tick)
                               * (1000 / SERVER_TICK_HZ);

        // Construir y firmar el record de vuelta
        Net::LapTimeRecord record;
        record.player_id      = car.player_id;
        record.lap_time_ms    = lap_time_ms;
        record.server_tick    = current_tick_;
        record.unix_timestamp = GetUnixTimestampMs();

        // Firmar con HMAC del servidor (solo el servidor puede producir records válidos)
        SignLapTime(record);

        if (on_lap_time_) on_lap_time_(record);

        // Preparar siguiente vuelta
        car.lap_start_tick = current_tick_;
        ResetCheckpoints(car);
        car.current_lap++;
    }

    // -------------------------------------------------------------------------
    void SaveStateHistory() {
        for (auto& [pid, car] : cars_) {
            car.state_history.push_back(car.physics);
            if (car.state_history.size() > STATE_HISTORY_TICKS)
                car.state_history.pop_front();
        }
    }

    // -------------------------------------------------------------------------
    void EmitWorldSnapshot() {
        Net::WorldSnapshotHeader header;
        header.server_tick  = current_tick_;
        header.race_time_ms = static_cast<uint32_t>(current_tick_ * 10);
        header.num_cars     = static_cast<uint8_t>(cars_.size());

        std::vector<Net::CarState> car_states;
        car_states.reserve(cars_.size());

        for (const auto& [pid, car] : cars_) {
            Net::CarState cs;
            cs.player_id = pid;
            cs.input_ack = car.last_input_seq;
            cs.pos_x = car.physics.position.x;
            cs.pos_y = car.physics.position.y;
            cs.pos_z = car.physics.position.z;
            cs.vel_x = car.physics.velocity.x;
            cs.vel_y = car.physics.velocity.y;
            cs.vel_z = car.physics.velocity.z;
            cs.rot_w = car.physics.rotation.w;
            cs.rot_x = car.physics.rotation.x;
            cs.rot_y = car.physics.rotation.y;
            cs.rot_z = car.physics.rotation.z;
            cs.engine_rpm = static_cast<uint16_t>(car.physics.engine_rpm);
            cs.gear       = static_cast<int8_t>(car.physics.current_gear);
            cs.damage_flags = car.physics.damage_flags;
            car_states.push_back(cs);
        }

        if (on_snapshot_) on_snapshot_(header, car_states);
    }

    // -------------------------------------------------------------------------
    void RecordCheat(ServerCarState& car, CheatType type,
                     float measured, float max_allowed)
    {
        CheatEvent event;
        event.player_id        = car.player_id;
        event.type             = type;
        event.measured_value   = measured;
        event.max_allowed_value = max_allowed;
        event.server_tick      = current_tick_;

        car.cheat_strike_count++;
        if (car.cheat_strike_count >= 3) {
            car.is_banned = true;
            // En producción: notificar al backend para ban temporal/permanente
        }

        if (on_cheat_) on_cheat_(event);
    }

    void SignLapTime(Net::LapTimeRecord& record) const {
        // HMAC-SHA256 del record usando la clave del servidor
        // En producción: llamar OpenSSL HMAC() como en VehicleSetup.cpp
        // Aquí marcado como placeholder
        std::memset(record.signature, 0xAB, 32);  // PLACEHOLDER
    }

    void ResetCheckpoints(ServerCarState& car) {
        std::fill(car.checkpoints_hit.begin(), car.checkpoints_hit.end(), false);
    }

    void LoadHMACKey(std::string_view hex) {
        for (size_t i = 0; i < std::min(hex.size() / 2, hmac_key_.size()); ++i) {
            char buf[3] = {hex[i*2], hex[i*2+1], '\0'};
            hmac_key_[i] = static_cast<uint8_t>(std::stoul(buf, nullptr, 16));
        }
    }

    static uint64_t GetUnixTimestampMs() {
        using namespace std::chrono;
        return static_cast<uint64_t>(
            duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
    }

    std::mutex mutex_;
};

} // namespace SimRacing
