#pragma once
// =============================================================================
// VehicleSetup.h
// Sistema de guardado, carga y validación criptográfica de setups de vehículo.
//
// Formato de archivo: .vsetup (JSON interno + AES-256-CBC + HMAC-SHA256)
// Requiere: OpenSSL 3.x, nlohmann/json
// =============================================================================

#include <string>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace SimRacing {

// =============================================================================
// Versiones del formato de Setup — nunca bajar este número
// =============================================================================
static constexpr uint32_t SETUP_FORMAT_VERSION = 2;
static constexpr uint32_t SETUP_MAGIC           = 0x53455455; // "SETU"

// =============================================================================
// Sub-estructuras del Setup
// =============================================================================

struct TireSetup {
    float pressure_front_psi  = 27.0f;   // Rango: [15.0, 45.0]
    float pressure_rear_psi   = 26.0f;   // Rango: [15.0, 45.0]
    float camber_front_deg    = -2.5f;   // Rango: [-5.0, 1.0]
    float camber_rear_deg     = -1.5f;   // Rango: [-4.0, 0.5]
    float toe_front_mm        = -0.5f;   // Negativo = toe-out (mm por rueda)
    float toe_rear_mm         =  1.0f;   // Positivo = toe-in
    float caster_deg          =  7.0f;   // Rango: [3.0, 12.0] (solo front)
};

struct SuspensionSetup {
    float spring_rate_front_nm   = 55000.0f; // N/m
    float spring_rate_rear_nm    = 60000.0f;
    float damper_bump_front      = 3500.0f;  // N·s/m (compresión)
    float damper_bump_rear       = 3800.0f;
    float damper_rebound_front   = 4200.0f;  // N·s/m (extensión)
    float damper_rebound_rear    = 4500.0f;
    float ride_height_front_mm   = 75.0f;    // Altura libre
    float ride_height_rear_mm    = 80.0f;
    float anti_roll_bar_front_nm = 12000.0f; // Rigidez ARB
    float anti_roll_bar_rear_nm  = 10000.0f;
};

struct GearboxSetup {
    static constexpr int MAX_GEARS = 8;
    float gear_ratios[MAX_GEARS] = {3.20f, 2.10f, 1.60f, 1.28f,
                                     1.05f, 0.88f, 0.74f, 0.00f};
    int   num_gears              = 7;    // Marchas activas (excl. reversa)
    float final_drive_ratio      = 3.73f;
    float diff_preload_nm        = 80.0f;   // Precarga LSD
    float diff_power_ramp_deg    = 45.0f;   // Rampa potencia (grados)
    float diff_coast_ramp_deg    = 60.0f;   // Rampa freno-motor
};

struct BrakeSetup {
    float bias_front_pct        = 0.58f;  // [0.40, 0.75] — 0.58 = 58% delante
    float pad_friction_front    = 0.42f;  // Coef. fricción pastilla
    float pad_friction_rear     = 0.38f;
    float duct_cooling_front    = 0.6f;   // [0.0, 1.0] — apertura conducto
    float duct_cooling_rear     = 0.4f;
};

struct AeroSetup {
    float front_downforce_kg    = 45.0f;   // A 200 km/h
    float rear_downforce_kg     = 65.0f;
    float drag_coefficient      = 0.31f;   // Cd total
    // Algunos coches tienen ajuste manual de ángulo de alerón
    float front_wing_angle_deg  = 5.0f;
    float rear_wing_angle_deg   = 12.0f;
};

struct FuelSetup {
    float fuel_load_kg          = 30.0f;   // Rango: [5.0, max_tank]
    bool  pit_fuel_strategy     = false;   // ¿Repostar en parada?
};

// =============================================================================
// Estructura principal del Setup
// =============================================================================

struct VehicleSetupData {
    // --- Metadatos ---
    uint32_t    format_version = SETUP_FORMAT_VERSION;
    char        vehicle_id[64] = {};      // ID del coche (ej: "car_gt3_001")
    char        setup_name[128] = {};     // Nombre del setup (ej: "Monza Q")
    char        track_id[64] = {};        // Pista para la que fue creado
    char        author_steam_id[32] = {}; // SteamID64 del creador
    uint64_t    created_timestamp = 0;    // Unix timestamp (UTC)
    uint64_t    modified_timestamp = 0;

    // --- Configuración del vehículo ---
    TireSetup       tires;
    SuspensionSetup suspension;
    GearboxSetup    gearbox;
    BrakeSetup      brakes;
    AeroSetup       aero;
    FuelSetup       fuel;

    // --- ABS / TC / SC / ERS ---
    uint8_t abs_level   = 5;   // [0=off, 1-10]
    uint8_t tc_level    = 4;   // [0=off, 1-10]
    uint8_t sc_level    = 3;   // [0=off, 1-10] stability control
    uint8_t ers_level   = 5;   // [0=charge, 10=max_deploy] (si aplica)

    // --- Checksum de integridad (calculado al guardar, verificado al cargar) ---
    // 32 bytes de HMAC-SHA256 sobre todos los campos anteriores
    uint8_t hmac_signature[32] = {};
};

// =============================================================================
// Resultado de operaciones de Setup
// =============================================================================

enum class SetupResult {
    OK,
    FILE_NOT_FOUND,
    READ_ERROR,
    WRITE_ERROR,
    INVALID_MAGIC,
    VERSION_MISMATCH,
    HMAC_INVALID,       // El archivo fue modificado externamente
    DECRYPT_FAILED,     // Clave incorrecta o archivo corrupto
    VALIDATION_FAILED,  // Valores fuera de rango
    JSON_PARSE_ERROR,
};

// =============================================================================
// Clase principal del sistema de Setup
// =============================================================================

class VehicleSetupManager {
public:
    explicit VehicleSetupManager();
    ~VehicleSetupManager() = default;

    VehicleSetupManager(const VehicleSetupManager&) = delete;
    VehicleSetupManager& operator=(const VehicleSetupManager&) = delete;

    // -------------------------------------------------------------------------
    // Guardar un setup en disco
    // Genera un HMAC-SHA256 y cifra con AES-256-CBC
    // El archivo resultante tiene extensión .vsetup
    // -------------------------------------------------------------------------
    SetupResult Save(const VehicleSetupData& setup,
                     const std::filesystem::path& filepath,
                     const std::string& player_token) const;

    // -------------------------------------------------------------------------
    // Cargar un setup desde disco
    // Descifra AES-256-CBC y verifica HMAC-SHA256
    // -------------------------------------------------------------------------
    SetupResult Load(const std::filesystem::path& filepath,
                     const std::string& player_token,
                     VehicleSetupData& out_setup) const;

    // -------------------------------------------------------------------------
    // Exportar setup como JSON legible (para intercambio en comunidad)
    // El JSON NO está cifrado, solo lleva HMAC para verificar integridad
    // -------------------------------------------------------------------------
    SetupResult ExportJSON(const VehicleSetupData& setup,
                           const std::filesystem::path& filepath) const;

    // -------------------------------------------------------------------------
    // Importar setup desde JSON de la comunidad
    // -------------------------------------------------------------------------
    SetupResult ImportJSON(const std::filesystem::path& filepath,
                           VehicleSetupData& out_setup) const;

    // -------------------------------------------------------------------------
    // Validar que los valores del setup están dentro de rangos físicos
    // Llamado automáticamente en Save() y Load()
    // -------------------------------------------------------------------------
    SetupResult ValidateSetupRanges(const VehicleSetupData& setup) const;

    // Descripción textual de un SetupResult para UI/log
    static std::string ResultToString(SetupResult result);

private:
    // Genera la clave de cifrado AES-256 a partir del token del jugador
    // usando PBKDF2-HMAC-SHA256 con salt derivado del vehicle_id
    void DeriveEncryptionKey(const std::string& player_token,
                             const char* vehicle_id,
                             uint8_t out_key[32],
                             uint8_t out_iv[16]) const;

    // Calcula HMAC-SHA256 de todos los campos del setup (excepto hmac_signature)
    void ComputeHMAC(const VehicleSetupData& setup,
                     uint8_t out_hmac[32]) const;

    // Verifica que el HMAC almacenado coincide con el calculado
    bool VerifyHMAC(const VehicleSetupData& setup) const;

    // Cifrado AES-256-CBC con padding PKCS7
    bool AES256Encrypt(const uint8_t* plaintext, size_t plaintext_len,
                       const uint8_t key[32], const uint8_t iv[16],
                       std::vector<uint8_t>& out_ciphertext) const;

    bool AES256Decrypt(const uint8_t* ciphertext, size_t ciphertext_len,
                       const uint8_t key[32], const uint8_t iv[16],
                       std::vector<uint8_t>& out_plaintext) const;

    // Serializa VehicleSetupData a JSON interno
    std::string SerializeToJSON(const VehicleSetupData& setup) const;

    // Deserializa JSON interno a VehicleSetupData
    bool DeserializeFromJSON(const std::string& json_str,
                             VehicleSetupData& out_setup) const;

    // Clave maestra del servidor para HMAC (se carga desde variable de entorno)
    static constexpr size_t MASTER_KEY_SIZE = 64;
    std::array<uint8_t, MASTER_KEY_SIZE> master_hmac_key_;
};

} // namespace SimRacing
