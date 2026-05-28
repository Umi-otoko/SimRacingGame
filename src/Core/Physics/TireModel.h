#pragma once
// =============================================================================
// TireModel.h
// Implementación del modelo de neumático Pacejka Magic Formula 96 (MF5.2)
// Corre en el Physics Thread a 100 Hz, desacoplado del Render Thread.
//
// Referencia: "Tyre and Vehicle Dynamics" — Hans B. Pacejka, 3rd Ed.
// =============================================================================

#include <cmath>
#include <array>

namespace SimRacing {

// Coeficientes del modelo Pacejka para un neumático específico
// Estos valores se cargan desde los archivos de datos del coche (.tiredata)
struct PacejkaCoefficients {
    // ---- Fuerza longitudinal (Fx) — slip ratio ----
    float Bx = 10.0f;   // Stiffness factor longitudinal
    float Cx = 1.65f;   // Shape factor
    float Dx = 1.10f;   // Peak factor (coef. fricción pico)
    float Ex = -0.50f;  // Curvature factor (< 0 = progresivo)

    // ---- Fuerza lateral (Fy) — slip angle ----
    float By = 8.50f;   // Stiffness factor lateral
    float Cy = 1.30f;   // Shape factor
    float Dy = 1.05f;   // Peak factor
    float Ey = -0.80f;  // Curvature factor

    // ---- Momento de auto-alineación (Mz) ----
    float Bt = 8.0f;
    float Ct = 1.5f;
    float Dt = 0.18f;
    float Et = -0.3f;

    // ---- Dependencia de carga vertical (Fz) ----
    float Fz_nominal_N = 3500.0f;  // Carga vertical nominal (N) — ~350 kg
    float lambda_mu    = 1.0f;     // Factor de escala de fricción (1.0 = seco)

    // Reducción de capacidad de agarre a temperatura extrema
    float temp_peak_celsius = 90.0f;    // Temperatura óptima de trabajo
    float temp_coeff_cold   = 0.75f;    // Multiplicador de grip a <40°C
    float temp_coeff_hot    = 0.85f;    // Multiplicador de grip a >130°C
};

struct TireContactPatch {
    float longitudinal_slip = 0.0f;  // Slip ratio κ — [-1.0, 1.0]
    float lateral_slip_rad  = 0.0f;  // Slip angle α — [-0.5, 0.5] rad (~28°)
    float camber_rad        = 0.0f;  // Camber angle γ — [-0.15, 0.15] rad
    float vertical_load_N   = 0.0f;  // Carga normal Fz
    float speed_mps         = 0.0f;  // Velocidad del centro de rueda
    float temperature_celsius = 85.0f;
};

struct TireForces {
    float Fx = 0.0f;  // Fuerza longitudinal (tracción/frenada) — N
    float Fy = 0.0f;  // Fuerza lateral (steering) — N
    float Mz = 0.0f;  // Momento de auto-alineación — N·m
};

// =============================================================================
// PacejkaTireModel — Calcula fuerzas del neumático por frame de física
// =============================================================================

class PacejkaTireModel {
public:
    explicit PacejkaTireModel(const PacejkaCoefficients& coeffs)
        : c_(coeffs) {}

    // -------------------------------------------------------------------------
    // Calcula las tres fuerzas del neumático para un estado de contacto dado.
    // Corre a 100 Hz en el Physics Thread.
    // -------------------------------------------------------------------------
    TireForces Evaluate(const TireContactPatch& contact) const {
        TireForces forces;

        if (contact.vertical_load_N <= 0.0f) return forces;  // Rueda en el aire

        const float Fz = contact.vertical_load_N;
        const float mu = ComputeFrictionCoefficient(contact);

        forces.Fx = ComputeLongitudinalForce(contact, Fz, mu);
        forces.Fy = ComputeLateralForce(contact, Fz, mu);
        forces.Mz = ComputeAligningTorque(contact, forces.Fy, Fz, mu);

        return forces;
    }

private:
    PacejkaCoefficients c_;

    // Factor de fricción dependiente de temperatura
    float ComputeFrictionCoefficient(const TireContactPatch& contact) const {
        float mu = c_.lambda_mu;

        const float T = contact.temperature_celsius;
        if (T < c_.temp_peak_celsius) {
            // Frío: interpolación lineal entre cold_coeff y 1.0
            float t = std::clamp((T - 20.0f) / (c_.temp_peak_celsius - 20.0f), 0.0f, 1.0f);
            mu *= c_.temp_coeff_cold + t * (1.0f - c_.temp_coeff_cold);
        } else if (T > c_.temp_peak_celsius + 30.0f) {
            // Sobrecalentamiento
            float t = std::clamp((T - (c_.temp_peak_celsius + 30.0f)) / 50.0f, 0.0f, 1.0f);
            mu *= 1.0f - t * (1.0f - c_.temp_coeff_hot);
        }

        return mu;
    }

    // -------------------------------------------------------------------------
    // Fuerza longitudinal Fx = Dx * sin(Cx * atan(Bx*κ - Ex*(Bx*κ - atan(Bx*κ))))
    // κ = longitudinal slip ratio
    // -------------------------------------------------------------------------
    float ComputeLongitudinalForce(const TireContactPatch& c,
                                    float Fz, float mu) const
    {
        const float kappa = c.longitudinal_slip;

        // Escalar pico según carga vertical (Fz)
        const float Dx = mu * c_.Dx * Fz;

        // Para velocidades muy bajas, suavizar el denominador del slip ratio
        // (evita divergencia a V ≈ 0 — problema conocido de Pacejka)
        const float speed_guard = std::max(std::abs(c.speed_mps), 0.5f);
        const float kappa_safe  = kappa * (c.speed_mps / speed_guard);

        return PacejkaMagicFormula(kappa_safe, c_.Bx, c_.Cx, Dx, c_.Ex);
    }

    // -------------------------------------------------------------------------
    // Fuerza lateral Fy = Dy * sin(Cy * atan(By*α - Ey*(By*α - atan(By*α))))
    // α = slip angle (rad)
    // -------------------------------------------------------------------------
    float ComputeLateralForce(const TireContactPatch& c,
                               float Fz, float mu) const
    {
        const float alpha  = c.lateral_slip_rad;
        const float gamma  = c.camber_rad;

        // La carga afecta el pico de manera no lineal (normalización respecto Fz_nom)
        const float Fz_norm = Fz / c_.Fz_nominal_N;
        const float Dy = mu * c_.Dy * Fz * (1.0f - 0.05f * Fz_norm);

        // Corrección de camber (el camber "desplaza" la curva lateralmente)
        const float Hy = std::sin(gamma) * 0.5f;  // Offset por camber

        return PacejkaMagicFormula(alpha + Hy, c_.By, c_.Cy, Dy, c_.Ey);
    }

    // -------------------------------------------------------------------------
    // Momento de auto-alineación Mz
    // Simplificado: proporcional a Fy con brazo de neumática pneumático (t)
    // -------------------------------------------------------------------------
    float ComputeAligningTorque(const TireContactPatch& contact,
                                 float Fy, float Fz, float mu) const
    {
        const float alpha = contact.lateral_slip_rad;
        const float Dt = mu * c_.Dt * Fz;

        // Longitud del trail neumático (pneumatic trail)
        const float trail = PacejkaMagicFormula(alpha, c_.Bt, c_.Ct, Dt, c_.Et);

        return -trail * Fy;
    }

    // -------------------------------------------------------------------------
    // Fórmula mágica de Pacejka (forma general)
    // y(x) = D * sin(C * atan(B*x - E*(B*x - atan(B*x))))
    // -------------------------------------------------------------------------
    static float PacejkaMagicFormula(float x, float B, float C, float D, float E) {
        const float Bx    = B * x;
        const float inner = Bx - E * (Bx - std::atan(Bx));
        return D * std::sin(C * std::atan(inner));
    }
};

// =============================================================================
// TireTemperatureModel — Modelo de temperatura del neumático
// Separa la temperatura en 3 capas: superficie, masa, nucleo (simplificado a 1 capa)
// =============================================================================

class TireTemperatureModel {
public:
    static constexpr float AMBIENT_TEMP_C = 25.0f;

    struct TireThermo {
        float surface_temp  = 60.0f;   // °C
        float core_temp     = 50.0f;   // °C — más lenta de calentar/enfriar
        float wear_factor   = 0.0f;    // [0.0, 1.0] — 1.0 = neumático destruido
    };

    TireThermo Update(const TireThermo& prev,
                      const TireContactPatch& contact,
                      float dt_s) const
    {
        TireThermo next = prev;

        // Calor generado por fricción de deslizamiento
        const float slip_energy = (std::abs(contact.longitudinal_slip) +
                                   std::abs(contact.lateral_slip_rad) * 0.8f)
                                  * contact.vertical_load_N
                                  * std::abs(contact.speed_mps);

        // Coeficiente de calentamiento y enfriamiento
        constexpr float heat_coeff   = 0.0003f;
        constexpr float cool_coeff   = 0.02f;  // Enfriamiento al ambiente
        constexpr float core_transfer = 0.005f; // Transferencia superficie → nucleo

        // Calentamiento por energía de fricción
        next.surface_temp += (slip_energy * heat_coeff) * dt_s;

        // Enfriamiento hacia ambiente
        next.surface_temp -= (next.surface_temp - AMBIENT_TEMP_C) * cool_coeff * dt_s;

        // Transferencia lenta hacia el núcleo
        float delta_core = (next.surface_temp - next.core_temp) * core_transfer * dt_s;
        next.core_temp  += delta_core;

        // Desgaste: proporcional a la energía de deslizamiento acumulada
        constexpr float wear_per_joule = 1e-8f;
        next.wear_factor += slip_energy * wear_per_joule * dt_s;
        next.wear_factor  = std::min(next.wear_factor, 1.0f);

        return next;
    }
};

} // namespace SimRacing
