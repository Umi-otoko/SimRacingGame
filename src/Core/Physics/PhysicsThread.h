#pragma once
// =============================================================================
// PhysicsThread.h
// Loop de física multi-hilo: Physics Tick a 100 Hz, independiente del
// Render Thread (144+ Hz) y del Game Thread (60 Hz).
//
// Arquitectura de hilos:
//   Render Thread  ──────────── VSync / DLSS / Nanite
//   Game Thread    ──────────── Input, gameplay logic, UI
//   Physics Thread ──────────── Jolt step, Pacejka tires, suspension
//   Network Thread ──────────── Send/recv UDP packets
// =============================================================================

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <vector>
#include <array>
#include <cmath>

#include "TireModel.h"

namespace SimRacing {

// Frequencia del physics tick (Hz)
static constexpr int PHYSICS_HZ    = 100;
static constexpr double PHYSICS_DT = 1.0 / PHYSICS_HZ;  // 0.01 segundos

// =============================================================================
// VehiclePhysicsState — Estado completo del vehículo en un instante T
// Esta estructura se intercambia con doble buffering entre Physics y Render thread.
// =============================================================================

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float Length() const { return std::sqrt(x*x + y*y + z*z); }
    Vec3 operator+(const Vec3& o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3 operator*(float s) const { return {x*s, y*s, z*s}; }
};

struct Quaternion {
    float w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;
};

struct WheelState {
    float angular_velocity_rad = 0.0f;  // Velocidad angular de la rueda (rad/s)
    float suspension_travel_m  = 0.0f;  // Recorrido de suspensión (m)
    float contact_normal_y     = 1.0f;  // Normal del suelo en contacto
    bool  in_contact           = true;  // ¿La rueda toca el suelo?
    float load_N               = 3500.0f;
    TireTemperatureModel::TireThermo thermo;
};

struct VehiclePhysicsState {
    // Timestamp para interpolación en Render Thread
    double timestamp_s = 0.0;
    uint64_t tick      = 0;

    // Transformada del chasis
    Vec3       position    = {};
    Vec3       velocity    = {};  // m/s
    Vec3       acceleration = {};
    Quaternion rotation    = {};
    Vec3       angular_velocity = {};  // rad/s

    // Cinemática del motor/transmisión
    float engine_rpm    = 1000.0f;
    float engine_torque = 0.0f;   // N·m
    int   current_gear  = 1;
    float speed_kmh     = 0.0f;
    float fuel_kg       = 30.0f;

    // Estado de las 4 ruedas [FL, FR, RL, RR]
    std::array<WheelState, 4> wheels;

    // Fuerzas aerodinámica
    float aero_downforce_N = 0.0f;
    float aero_drag_N      = 0.0f;

    // Daños
    uint8_t damage_flags   = 0;  // bits: FL=0, FR=1, RL=2, RR=3, engine=4, aero=5
};

// =============================================================================
// PlayerInput — Input del jugador para este tick de física
// =============================================================================

struct PlayerInput {
    uint64_t sequence_number = 0;
    double   timestamp_s     = 0.0;

    float throttle  = 0.0f;   // [0.0, 1.0]
    float brake     = 0.0f;   // [0.0, 1.0]
    float steering  = 0.0f;   // [-1.0, 1.0]
    float clutch    = 0.0f;   // [0.0, 1.0]
    int8_t gear     = 1;      // [-1=R, 0=N, 1-7]
    bool   handbrake = false;
};

// =============================================================================
// PhysicsThread — Loop de física en hilo dedicado
// =============================================================================

class PhysicsThread {
public:
    using StateCallback = std::function<void(const VehiclePhysicsState&)>;

    PhysicsThread() : running_(false), paused_(false) {
        // Inicializar con coeficientes por defecto del neumático
        tire_model_ = std::make_unique<PacejkaTireModel>(PacejkaCoefficients{});
    }

    ~PhysicsThread() { Stop(); }

    // -------------------------------------------------------------------------
    // Iniciar el hilo de física
    // on_state_ready: callback llamado cada tick con el estado actualizado
    // Se llama desde el Physics Thread — sincronizar adecuadamente con Render
    // -------------------------------------------------------------------------
    void Start(StateCallback on_state_ready) {
        state_callback_ = std::move(on_state_ready);
        running_ = true;
        thread_  = std::thread(&PhysicsThread::PhysicsLoop, this);

        // Elevar prioridad del hilo de física para garantizar 100 Hz constante
#ifdef _WIN32
        SetThreadPriority(thread_.native_handle(), THREAD_PRIORITY_HIGHEST);
#endif
    }

    void Stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

    void Pause()  { paused_ = true;  }
    void Resume() { paused_ = false; }

    // -------------------------------------------------------------------------
    // Inyectar input del jugador (thread-safe, llamado desde Game Thread)
    // -------------------------------------------------------------------------
    void SetInput(const PlayerInput& input) {
        std::lock_guard<std::mutex> lock(input_mutex_);
        pending_input_ = input;
        has_new_input_ = true;
    }

    // -------------------------------------------------------------------------
    // Obtener una copia thread-safe del estado más reciente (para Render Thread)
    // -------------------------------------------------------------------------
    VehiclePhysicsState GetLatestState() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return render_state_;  // Doble buffering: este estado es siempre completo
    }

    // -------------------------------------------------------------------------
    // Forzar un estado externo (usado por el cliente para Server Reconciliation)
    // -------------------------------------------------------------------------
    void SetAuthorativeState(const VehiclePhysicsState& server_state) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        physics_state_ = server_state;
    }

private:
    std::thread thread_;
    std::atomic<bool> running_;
    std::atomic<bool> paused_;

    mutable std::mutex input_mutex_;
    mutable std::mutex state_mutex_;

    PlayerInput          pending_input_;
    std::atomic<bool>    has_new_input_{false};
    VehiclePhysicsState  physics_state_;   // Estado en Physics Thread
    VehiclePhysicsState  render_state_;    // Copia para Render Thread (doble buffer)

    StateCallback state_callback_;
    std::unique_ptr<PacejkaTireModel> tire_model_;

    // -------------------------------------------------------------------------
    // Loop principal — se ejecuta en el Physics Thread
    // Objetivo: mantener exactamente 100 Hz, acumular deficit si hay retraso
    // -------------------------------------------------------------------------
    void PhysicsLoop() {
        using Clock   = std::chrono::steady_clock;
        using Duration = std::chrono::duration<double>;

        auto target_dt = std::chrono::duration<double>(PHYSICS_DT);
        auto next_tick = Clock::now() + target_dt;

        double sim_time = 0.0;

        while (running_) {
            // Esperar hasta el próximo tick
            std::this_thread::sleep_until(next_tick);
            next_tick += target_dt;

            if (paused_) continue;

            // Leer input del Game Thread
            PlayerInput current_input;
            if (has_new_input_.exchange(false)) {
                std::lock_guard<std::mutex> lock(input_mutex_);
                current_input = pending_input_;
            }

            // Paso de simulación
            Step(current_input, static_cast<float>(PHYSICS_DT), sim_time);
            sim_time += PHYSICS_DT;

            // Publicar estado para Render Thread (doble buffer)
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                render_state_ = physics_state_;
            }

            // Notificar callback (Network Thread usa esto para enviar al servidor)
            if (state_callback_) state_callback_(physics_state_);
        }
    }

    // -------------------------------------------------------------------------
    // Step — Simula un tick de física de 10ms
    // -------------------------------------------------------------------------
    void Step(const PlayerInput& input, float dt, double sim_time) {
        physics_state_.tick++;
        physics_state_.timestamp_s = sim_time;

        // 1. Actualizar RPM y torque del motor
        UpdateEngine(input, dt);

        // 2. Calcular fuerzas de neumáticos (Pacejka) para cada rueda
        UpdateTireForces(input, dt);

        // 3. Integrar dinámica del chasis (Euler semi-implícito)
        IntegrateChasis(dt);

        // 4. Actualizar métricas derivadas
        physics_state_.speed_kmh = physics_state_.velocity.Length() * 3.6f;
    }

    void UpdateEngine(const PlayerInput& input, float dt) {
        // Curva de torque simplificada (se reemplaza con tabla de lookup en producción)
        // Torque máximo entre 3500-6000 RPM, caída por encima y debajo
        const float rpm = physics_state_.engine_rpm;
        const float peak_torque = 420.0f;  // N·m — ejemplo GT3

        float torque_norm;
        if      (rpm < 1000.0f) torque_norm = 0.3f;
        else if (rpm < 3500.0f) torque_norm = 0.3f + (rpm - 1000.0f) / 2500.0f * 0.7f;
        else if (rpm < 6500.0f) torque_norm = 1.0f;
        else if (rpm < 8500.0f) torque_norm = 1.0f - (rpm - 6500.0f) / 2000.0f * 0.4f;
        else                    torque_norm = 0.0f;  // Rev limiter

        float drive_torque = peak_torque * torque_norm * input.throttle;
        float brake_torque = 2200.0f * input.brake;  // N·m en los frenos

        physics_state_.engine_torque = drive_torque;

        // Aceleración aproximada del RPM (transmisión simplificada)
        const float inertia = 0.15f;  // kg·m² del cigüeñal
        float net_torque = drive_torque - brake_torque * 0.1f;  // Freno-motor
        float rpm_accel = (net_torque / inertia) * (60.0f / (2.0f * 3.14159f));
        physics_state_.engine_rpm += rpm_accel * dt;
        physics_state_.engine_rpm  = std::clamp(physics_state_.engine_rpm, 800.0f, 8600.0f);
    }

    void UpdateTireForces(const PlayerInput& input, float dt) {
        const float speed = physics_state_.velocity.Length();

        for (int w = 0; w < 4; ++w) {
            auto& wheel = physics_state_.wheels[w];
            if (!wheel.in_contact) continue;

            TireContactPatch contact;
            contact.vertical_load_N  = wheel.load_N;
            contact.speed_mps        = speed;
            contact.temperature_celsius = wheel.thermo.surface_temp;

            // Slip ratio longitudinal
            const float wheel_speed = wheel.angular_velocity_rad * 0.33f;  // r = 0.33m
            contact.longitudinal_slip = (speed > 0.5f)
                ? (wheel_speed - speed) / std::max(speed, wheel_speed)
                : 0.0f;

            // Ángulo de deslizamiento lateral (simplificado)
            contact.lateral_slip_rad = -input.steering * 0.25f;  // Max ~14°

            // Calcular fuerzas Pacejka
            TireForces forces = tire_model_->Evaluate(contact);

            // Actualizar temperatura del neumático
            wheel.thermo = TireTemperatureModel{}.Update(wheel.thermo, contact, dt);

            // Las fuerzas se aplican al chasis (integración en IntegrateChasis)
            // En producción: pasar forces al solver de Jolt
            (void)forces;
        }
    }

    void IntegrateChasis(float dt) {
        // Euler semi-implícito básico (Jolt Physics maneja la integración real)
        // Aquí solo para demo — en producción: jolt_world.Step(dt)
        auto& state = physics_state_;

        // Integrar velocidad → posición
        state.position = state.position + state.velocity * dt;

        // Resistencia aerodinámica simplificada
        const float speed = state.velocity.Length();
        if (speed > 0.01f) {
            const float drag = state.aero_drag_N / std::max(speed, 0.1f);
            state.velocity.x *= (1.0f - drag * dt * 0.01f);
            state.velocity.z *= (1.0f - drag * dt * 0.01f);
        }
    }
};

} // namespace SimRacing
