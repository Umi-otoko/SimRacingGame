#pragma once
// =============================================================================
// ClientPrediction.h
// Client-Side Prediction + Server Reconciliation para el coche del jugador.
//
// Flujo:
//   1. Game Thread: input → predice localmente → envía al servidor
//   2. Servidor: procesa inputs → envía snapshot autoritativo
//   3. Este módulo: compara predicción vs snapshot → reconcilia si divergen
// =============================================================================

#include "NetworkTypes.h"
#include "../Physics/PhysicsThread.h"
#include <deque>
#include <functional>
#include <mutex>
#include <cmath>

namespace SimRacing {

static constexpr float  RECONCILE_POSITION_THRESHOLD = 0.05f;  // 5 cm
static constexpr float  RECONCILE_VELOCITY_THRESHOLD = 0.5f;   // 0.5 m/s
static constexpr size_t INPUT_HISTORY_MAX_SIZE        = 256;    // ~2.5 seg a 100 Hz
static constexpr float  VISUAL_SMOOTH_ALPHA           = 0.25f;  // Suavizado visual

// =============================================================================
// InputHistoryEntry — Almacena input + estado predicho para re-simulación
// =============================================================================

struct InputHistoryEntry {
    PlayerInput          input;
    VehiclePhysicsState  predicted_state; // Estado ANTES de aplicar este input
};

// =============================================================================
// GhostCarInterpolator — Interpola los coches de otros jugadores
// =============================================================================

struct GhostSnapshot {
    uint64_t server_tick    = 0;
    double   timestamp_s    = 0.0;
    Vec3     position       = {};
    Vec3     velocity       = {};
    Quaternion rotation     = {};
};

class GhostCarInterpolator {
public:
    static constexpr double INTERP_DELAY_S = 0.100;  // 100ms de buffer para interpolación

    void PushSnapshot(const GhostSnapshot& snap) {
        if (!snapshots_.empty() && snap.server_tick <= snapshots_.back().server_tick)
            return;  // Descartar snapshot antiguo o duplicado
        snapshots_.push_back(snap);
        if (snapshots_.size() > 16) snapshots_.pop_front();  // Mantener solo últimos 16
    }

    // Devuelve la posición/rotación interpolada para el tiempo de render dado
    bool Interpolate(double render_time, Vec3& out_pos, Quaternion& out_rot) const {
        const double target_time = render_time - INTERP_DELAY_S;

        if (snapshots_.size() < 2) {
            // Sin datos suficientes: dead reckoning si hay al menos 1 snapshot
            if (!snapshots_.empty()) {
                const auto& s = snapshots_.back();
                const double dt = render_time - s.timestamp_s;
                out_pos = s.position + s.velocity * static_cast<float>(dt);
                out_rot = s.rotation;
                return true;
            }
            return false;
        }

        // Buscar el par de snapshots que rodea target_time
        for (size_t i = 0; i + 1 < snapshots_.size(); ++i) {
            const auto& s0 = snapshots_[i];
            const auto& s1 = snapshots_[i + 1];

            if (target_time >= s0.timestamp_s && target_time <= s1.timestamp_s) {
                float t = static_cast<float>(
                    (target_time - s0.timestamp_s) / (s1.timestamp_s - s0.timestamp_s));
                t = std::clamp(t, 0.0f, 1.0f);

                out_pos = LerpVec3(s0.position, s1.position, t);
                out_rot = SlerpQuaternion(s0.rotation, s1.rotation, t);
                return true;
            }
        }

        // Extrapolación con el snapshot más reciente (coche "al frente" del buffer)
        const auto& latest = snapshots_.back();
        const double dt = render_time - latest.timestamp_s;
        if (dt < 0.5) {  // Solo extrapolar hasta 500ms
            out_pos = latest.position + latest.velocity * static_cast<float>(dt);
            out_rot = latest.rotation;
            return true;
        }

        return false;  // Datos demasiado viejos
    }

private:
    std::deque<GhostSnapshot> snapshots_;

    static Vec3 LerpVec3(const Vec3& a, const Vec3& b, float t) {
        return {a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t};
    }

    static Quaternion SlerpQuaternion(const Quaternion& a, const Quaternion& b, float t) {
        // Slerp simplificado: nlerp (normalizado lerp — error <0.1° para ángulos pequeños)
        float dot = a.w*b.w + a.x*b.x + a.y*b.y + a.z*b.z;

        // Si dot < 0, invertir b para tomar el camino corto
        Quaternion b_adj = b;
        if (dot < 0.0f) {
            b_adj.w = -b.w; b_adj.x = -b.x;
            b_adj.y = -b.y; b_adj.z = -b.z;
            dot = -dot;
        }

        Quaternion result;
        result.w = a.w + (b_adj.w - a.w) * t;
        result.x = a.x + (b_adj.x - a.x) * t;
        result.y = a.y + (b_adj.y - a.y) * t;
        result.z = a.z + (b_adj.z - a.z) * t;

        // Normalizar
        float mag = std::sqrt(result.w*result.w + result.x*result.x +
                              result.y*result.y + result.z*result.z);
        result.w /= mag; result.x /= mag;
        result.y /= mag; result.z /= mag;
        return result;
    }
};

// =============================================================================
// ClientPredictionSystem — Clase principal del sistema de predicción cliente
// =============================================================================

class ClientPredictionSystem {
public:
    using ResimulateFunc = std::function<
        VehiclePhysicsState(const VehiclePhysicsState&, const PlayerInput&, float dt)>;

    // resimulate_fn: función que aplica un input a un estado y devuelve el siguiente.
    // En producción, esta función llama al mismo Physics Thread (re-simulación rápida).
    explicit ClientPredictionSystem(ResimulateFunc resimulate_fn)
        : resimulate_fn_(std::move(resimulate_fn))
        , sequence_counter_(0)
        , last_reconcile_error_(0.0f)
    {}

    // -------------------------------------------------------------------------
    // RecordInput — Llamado por Game Thread cada frame de input
    // Aplica el input localmente y lo encola para enviar al servidor
    // -------------------------------------------------------------------------
    PlayerInput RecordInput(const PlayerInput& raw_input,
                             const VehiclePhysicsState& current_state,
                             float dt)
    {
        std::lock_guard<std::mutex> lock(history_mutex_);

        PlayerInput input = raw_input;
        input.sequence_number = ++sequence_counter_;
        input.timestamp_s     = current_state.timestamp_s;

        // Guardar par (estado antes del input + el input) para reconciliación futura
        InputHistoryEntry entry;
        entry.input           = input;
        entry.predicted_state = current_state;
        history_.push_back(std::move(entry));

        // Limpiar entradas viejas
        while (history_.size() > INPUT_HISTORY_MAX_SIZE)
            history_.pop_front();

        return input;
    }

    // -------------------------------------------------------------------------
    // ProcessServerSnapshot — Llamado cuando llega un snapshot autoritativo del servidor
    // Decide si reconciliar y cómo.
    // -------------------------------------------------------------------------
    void ProcessServerSnapshot(const Net::CarState& server_state,
                                VehiclePhysicsState& local_state)
    {
        std::lock_guard<std::mutex> lock(history_mutex_);

        // 1. Encontrar el estado local que corresponde al input_ack del servidor
        const uint64_t ack_seq = server_state.input_ack;

        // Calcular error de predicción (distancia entre posición predicha y autoritativa)
        const float dx = server_state.pos_x - local_state.position.x;
        const float dy = server_state.pos_y - local_state.position.y;
        const float dz = server_state.pos_z - local_state.position.z;
        last_reconcile_error_ = std::sqrt(dx*dx + dy*dy + dz*dz);

        // 2. Si el error es mínimo, no hacer nada (evitar jitter visual)
        if (last_reconcile_error_ < RECONCILE_POSITION_THRESHOLD) {
            PruneHistoryUpTo(ack_seq);
            return;
        }

        // 3. Reconciliación: retroceder al estado autoritativo del servidor
        VehiclePhysicsState reconciled;
        reconciled.position   = {server_state.pos_x, server_state.pos_y, server_state.pos_z};
        reconciled.velocity   = {server_state.vel_x, server_state.vel_y, server_state.vel_z};
        reconciled.rotation   = {server_state.rot_w, server_state.rot_x,
                                  server_state.rot_y, server_state.rot_z};
        reconciled.engine_rpm = server_state.engine_rpm;
        reconciled.current_gear = server_state.gear;

        // 4. Re-simular todos los inputs desde ack_seq hasta el presente
        for (const auto& entry : history_) {
            if (entry.input.sequence_number <= ack_seq) continue;
            reconciled = resimulate_fn_(reconciled, entry.input,
                                         static_cast<float>(PHYSICS_DT));
        }

        // 5. Aplicar corrección con suavizado visual (evitar "snap" brusco)
        local_state.position.x += (reconciled.position.x - local_state.position.x)
                                    * VISUAL_SMOOTH_ALPHA;
        local_state.position.y += (reconciled.position.y - local_state.position.y)
                                    * VISUAL_SMOOTH_ALPHA;
        local_state.position.z += (reconciled.position.z - local_state.position.z)
                                    * VISUAL_SMOOTH_ALPHA;
        local_state.velocity = reconciled.velocity;
        local_state.rotation = reconciled.rotation;

        // 6. Limpiar historial de inputs ya procesados
        PruneHistoryUpTo(ack_seq);
    }

    float GetLastReconcileError() const { return last_reconcile_error_; }
    uint64_t GetCurrentSequence() const { return sequence_counter_; }
    size_t GetHistorySize() const {
        std::lock_guard<std::mutex> lock(history_mutex_);
        return history_.size();
    }

private:
    mutable std::mutex history_mutex_;
    std::deque<InputHistoryEntry> history_;
    ResimulateFunc resimulate_fn_;
    uint64_t sequence_counter_;
    float last_reconcile_error_;

    void PruneHistoryUpTo(uint64_t ack_seq) {
        while (!history_.empty() &&
               history_.front().input.sequence_number <= ack_seq)
            history_.pop_front();
    }
};

} // namespace SimRacing
