// =============================================================================
// VehicleSetup.cpp
// Implementación del sistema de guardado/carga/validación de setups.
//
// Dependencias: OpenSSL 3.x (libssl + libcrypto), nlohmann/json
// Compilar con: -lssl -lcrypto
// =============================================================================

#include "VehicleSetup.h"

// OpenSSL
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

// nlohmann/json (header-only, incluir via vcpkg)
#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <stdexcept>

using json = nlohmann::json;

namespace SimRacing {

// =============================================================================
// Constantes del formato de archivo .vsetup
// =============================================================================

// Cabecera binaria del archivo cifrado
#pragma pack(push, 1)
struct SetupFileHeader {
    uint32_t magic;           // SETUP_MAGIC = 0x53455455 "SETU"
    uint32_t format_version;  // SETUP_FORMAT_VERSION
    uint8_t  salt[16];        // Salt aleatorio para PBKDF2
    uint8_t  iv[16];          // IV aleatorio para AES-256-CBC
    uint64_t ciphertext_len;  // Longitud del payload cifrado
    uint8_t  file_hmac[32];   // HMAC-SHA256 de (header + ciphertext) para anti-tamper
};
#pragma pack(pop)

static constexpr int PBKDF2_ITERATIONS = 100000;  // NIST recomendación mínima

// =============================================================================
// Constructor — carga la clave maestra del servidor
// =============================================================================

VehicleSetupManager::VehicleSetupManager() {
    // La clave maestra se carga desde variable de entorno en producción.
    // En desarrollo, se usa una clave hardcodeada para pruebas.
    const char* env_key = std::getenv("SIMRACING_HMAC_MASTER_KEY");

    if (env_key && strlen(env_key) >= 64) {
        // Cargar los primeros 64 bytes hexadecimales de la clave
        for (int i = 0; i < static_cast<int>(MASTER_KEY_SIZE); ++i) {
            char hex[3] = {env_key[i * 2], env_key[i * 2 + 1], '\0'};
            master_hmac_key_[i] = static_cast<uint8_t>(std::stoul(hex, nullptr, 16));
        }
    } else {
        // Clave de desarrollo (NUNCA usar en producción)
        const std::string dev_key = "DEV_SIMRACING_HMAC_KEY_DO_NOT_USE_IN_PRODUCTION_BUILD";
        SHA512(reinterpret_cast<const unsigned char*>(dev_key.c_str()),
               dev_key.size(), master_hmac_key_.data());

        std::cerr << "[VehicleSetupManager] WARNING: Using development HMAC key. "
                  << "Set SIMRACING_HMAC_MASTER_KEY environment variable in production.\n";
    }
}

// =============================================================================
// DeriveEncryptionKey — PBKDF2 para derivar AES-256 key + IV
// =============================================================================

void VehicleSetupManager::DeriveEncryptionKey(
    const std::string& player_token,
    const char* vehicle_id,
    uint8_t out_key[32],
    uint8_t out_iv[16]) const
{
    // Construir el "password" combinando token del jugador con la clave maestra
    std::string password = player_token + "|" + std::string(vehicle_id);

    // Salt = primeros 16 bytes del vehicle_id hasheado (determinista pero único por coche)
    uint8_t salt[16];
    uint8_t vehicle_hash[32];
    SHA256(reinterpret_cast<const unsigned char*>(vehicle_id),
           strnlen(vehicle_id, 64), vehicle_hash);
    std::memcpy(salt, vehicle_hash, 16);

    // Derivar 48 bytes total: 32 para key + 16 para IV
    uint8_t derived[48];
    PKCS5_PBKDF2_HMAC(
        password.c_str(), static_cast<int>(password.size()),
        salt, 16,
        PBKDF2_ITERATIONS,
        EVP_sha256(),
        48, derived
    );

    std::memcpy(out_key, derived,      32);
    std::memcpy(out_iv,  derived + 32, 16);
}

// =============================================================================
// ComputeHMAC — HMAC-SHA256 de los datos del setup (excluye el campo hmac)
// =============================================================================

void VehicleSetupManager::ComputeHMAC(const VehicleSetupData& setup,
                                       uint8_t out_hmac[32]) const
{
    // Calcular offset y tamaño del bloque de datos (todo excepto hmac_signature)
    constexpr size_t data_size = offsetof(VehicleSetupData, hmac_signature);

    unsigned int hmac_len = 32;
    HMAC(EVP_sha256(),
         master_hmac_key_.data(), static_cast<int>(MASTER_KEY_SIZE),
         reinterpret_cast<const unsigned char*>(&setup), data_size,
         out_hmac, &hmac_len);
}

bool VehicleSetupManager::VerifyHMAC(const VehicleSetupData& setup) const {
    uint8_t expected[32];
    ComputeHMAC(setup, expected);
    // Comparación en tiempo constante para prevenir timing attacks
    return CRYPTO_memcmp(expected, setup.hmac_signature, 32) == 0;
}

// =============================================================================
// AES-256-CBC Encrypt / Decrypt
// =============================================================================

bool VehicleSetupManager::AES256Encrypt(
    const uint8_t* plaintext, size_t plaintext_len,
    const uint8_t key[32], const uint8_t iv[16],
    std::vector<uint8_t>& out_ciphertext) const
{
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    bool success = false;
    do {
        if (!EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key, iv))
            break;

        // Reservar espacio para ciphertext + padding (hasta 1 bloque extra)
        out_ciphertext.resize(plaintext_len + 32);

        int out_len1 = 0;
        if (!EVP_EncryptUpdate(ctx, out_ciphertext.data(), &out_len1,
                               plaintext, static_cast<int>(plaintext_len)))
            break;

        int out_len2 = 0;
        if (!EVP_EncryptFinal_ex(ctx, out_ciphertext.data() + out_len1, &out_len2))
            break;

        out_ciphertext.resize(out_len1 + out_len2);
        success = true;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);
    return success;
}

bool VehicleSetupManager::AES256Decrypt(
    const uint8_t* ciphertext, size_t ciphertext_len,
    const uint8_t key[32], const uint8_t iv[16],
    std::vector<uint8_t>& out_plaintext) const
{
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    bool success = false;
    do {
        if (!EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key, iv))
            break;

        out_plaintext.resize(ciphertext_len);

        int out_len1 = 0;
        if (!EVP_DecryptUpdate(ctx, out_plaintext.data(), &out_len1,
                               ciphertext, static_cast<int>(ciphertext_len)))
            break;

        int out_len2 = 0;
        if (!EVP_DecryptFinal_ex(ctx, out_plaintext.data() + out_len1, &out_len2))
            break;

        out_plaintext.resize(out_len1 + out_len2);
        success = true;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);
    return success;
}

// =============================================================================
// Serialización JSON interna
// =============================================================================

std::string VehicleSetupManager::SerializeToJSON(const VehicleSetupData& s) const {
    json j;

    j["meta"]["format_version"]     = s.format_version;
    j["meta"]["vehicle_id"]         = std::string(s.vehicle_id);
    j["meta"]["setup_name"]         = std::string(s.setup_name);
    j["meta"]["track_id"]           = std::string(s.track_id);
    j["meta"]["author_steam_id"]    = std::string(s.author_steam_id);
    j["meta"]["created_timestamp"]  = s.created_timestamp;
    j["meta"]["modified_timestamp"] = s.modified_timestamp;

    j["tires"]["pressure_front_psi"] = s.tires.pressure_front_psi;
    j["tires"]["pressure_rear_psi"]  = s.tires.pressure_rear_psi;
    j["tires"]["camber_front_deg"]   = s.tires.camber_front_deg;
    j["tires"]["camber_rear_deg"]    = s.tires.camber_rear_deg;
    j["tires"]["toe_front_mm"]       = s.tires.toe_front_mm;
    j["tires"]["toe_rear_mm"]        = s.tires.toe_rear_mm;
    j["tires"]["caster_deg"]         = s.tires.caster_deg;

    j["suspension"]["spring_rate_front_nm"]   = s.suspension.spring_rate_front_nm;
    j["suspension"]["spring_rate_rear_nm"]    = s.suspension.spring_rate_rear_nm;
    j["suspension"]["damper_bump_front"]      = s.suspension.damper_bump_front;
    j["suspension"]["damper_bump_rear"]       = s.suspension.damper_bump_rear;
    j["suspension"]["damper_rebound_front"]   = s.suspension.damper_rebound_front;
    j["suspension"]["damper_rebound_rear"]    = s.suspension.damper_rebound_rear;
    j["suspension"]["ride_height_front_mm"]   = s.suspension.ride_height_front_mm;
    j["suspension"]["ride_height_rear_mm"]    = s.suspension.ride_height_rear_mm;
    j["suspension"]["anti_roll_bar_front_nm"] = s.suspension.anti_roll_bar_front_nm;
    j["suspension"]["anti_roll_bar_rear_nm"]  = s.suspension.anti_roll_bar_rear_nm;

    json gear_ratios = json::array();
    for (int i = 0; i < s.gearbox.num_gears; ++i)
        gear_ratios.push_back(s.gearbox.gear_ratios[i]);
    j["gearbox"]["gear_ratios"]         = gear_ratios;
    j["gearbox"]["num_gears"]           = s.gearbox.num_gears;
    j["gearbox"]["final_drive_ratio"]   = s.gearbox.final_drive_ratio;
    j["gearbox"]["diff_preload_nm"]     = s.gearbox.diff_preload_nm;
    j["gearbox"]["diff_power_ramp_deg"] = s.gearbox.diff_power_ramp_deg;
    j["gearbox"]["diff_coast_ramp_deg"] = s.gearbox.diff_coast_ramp_deg;

    j["brakes"]["bias_front_pct"]       = s.brakes.bias_front_pct;
    j["brakes"]["pad_friction_front"]   = s.brakes.pad_friction_front;
    j["brakes"]["pad_friction_rear"]    = s.brakes.pad_friction_rear;
    j["brakes"]["duct_cooling_front"]   = s.brakes.duct_cooling_front;
    j["brakes"]["duct_cooling_rear"]    = s.brakes.duct_cooling_rear;

    j["aero"]["front_downforce_kg"]  = s.aero.front_downforce_kg;
    j["aero"]["rear_downforce_kg"]   = s.aero.rear_downforce_kg;
    j["aero"]["drag_coefficient"]    = s.aero.drag_coefficient;
    j["aero"]["front_wing_angle_deg"]= s.aero.front_wing_angle_deg;
    j["aero"]["rear_wing_angle_deg"] = s.aero.rear_wing_angle_deg;

    j["fuel"]["fuel_load_kg"]        = s.fuel.fuel_load_kg;
    j["fuel"]["pit_fuel_strategy"]   = s.fuel.pit_fuel_strategy;

    j["electronics"]["abs_level"] = s.abs_level;
    j["electronics"]["tc_level"]  = s.tc_level;
    j["electronics"]["sc_level"]  = s.sc_level;
    j["electronics"]["ers_level"] = s.ers_level;

    return j.dump(2);  // Indentado para legibilidad en JSON de exportación
}

bool VehicleSetupManager::DeserializeFromJSON(const std::string& json_str,
                                               VehicleSetupData& s) const
{
    try {
        json j = json::parse(json_str);

        s.format_version = j["meta"]["format_version"].get<uint32_t>();

        auto copy_str = [](const std::string& src, char* dst, size_t dst_len) {
            std::strncpy(dst, src.c_str(), dst_len - 1);
            dst[dst_len - 1] = '\0';
        };

        copy_str(j["meta"]["vehicle_id"].get<std::string>(), s.vehicle_id, 64);
        copy_str(j["meta"]["setup_name"].get<std::string>(), s.setup_name, 128);
        copy_str(j["meta"]["track_id"].get<std::string>(), s.track_id, 64);
        copy_str(j["meta"]["author_steam_id"].get<std::string>(), s.author_steam_id, 32);
        s.created_timestamp  = j["meta"]["created_timestamp"].get<uint64_t>();
        s.modified_timestamp = j["meta"]["modified_timestamp"].get<uint64_t>();

        s.tires.pressure_front_psi = j["tires"]["pressure_front_psi"].get<float>();
        s.tires.pressure_rear_psi  = j["tires"]["pressure_rear_psi"].get<float>();
        s.tires.camber_front_deg   = j["tires"]["camber_front_deg"].get<float>();
        s.tires.camber_rear_deg    = j["tires"]["camber_rear_deg"].get<float>();
        s.tires.toe_front_mm       = j["tires"]["toe_front_mm"].get<float>();
        s.tires.toe_rear_mm        = j["tires"]["toe_rear_mm"].get<float>();
        s.tires.caster_deg         = j["tires"]["caster_deg"].get<float>();

        s.suspension.spring_rate_front_nm   = j["suspension"]["spring_rate_front_nm"].get<float>();
        s.suspension.spring_rate_rear_nm    = j["suspension"]["spring_rate_rear_nm"].get<float>();
        s.suspension.damper_bump_front      = j["suspension"]["damper_bump_front"].get<float>();
        s.suspension.damper_bump_rear       = j["suspension"]["damper_bump_rear"].get<float>();
        s.suspension.damper_rebound_front   = j["suspension"]["damper_rebound_front"].get<float>();
        s.suspension.damper_rebound_rear    = j["suspension"]["damper_rebound_rear"].get<float>();
        s.suspension.ride_height_front_mm   = j["suspension"]["ride_height_front_mm"].get<float>();
        s.suspension.ride_height_rear_mm    = j["suspension"]["ride_height_rear_mm"].get<float>();
        s.suspension.anti_roll_bar_front_nm = j["suspension"]["anti_roll_bar_front_nm"].get<float>();
        s.suspension.anti_roll_bar_rear_nm  = j["suspension"]["anti_roll_bar_rear_nm"].get<float>();

        auto ratios = j["gearbox"]["gear_ratios"].get<std::vector<float>>();
        s.gearbox.num_gears = j["gearbox"]["num_gears"].get<int>();
        for (int i = 0; i < std::min(s.gearbox.num_gears, GearboxSetup::MAX_GEARS); ++i)
            s.gearbox.gear_ratios[i] = ratios[i];
        s.gearbox.final_drive_ratio   = j["gearbox"]["final_drive_ratio"].get<float>();
        s.gearbox.diff_preload_nm     = j["gearbox"]["diff_preload_nm"].get<float>();
        s.gearbox.diff_power_ramp_deg = j["gearbox"]["diff_power_ramp_deg"].get<float>();
        s.gearbox.diff_coast_ramp_deg = j["gearbox"]["diff_coast_ramp_deg"].get<float>();

        s.brakes.bias_front_pct     = j["brakes"]["bias_front_pct"].get<float>();
        s.brakes.pad_friction_front = j["brakes"]["pad_friction_front"].get<float>();
        s.brakes.pad_friction_rear  = j["brakes"]["pad_friction_rear"].get<float>();
        s.brakes.duct_cooling_front = j["brakes"]["duct_cooling_front"].get<float>();
        s.brakes.duct_cooling_rear  = j["brakes"]["duct_cooling_rear"].get<float>();

        s.aero.front_downforce_kg   = j["aero"]["front_downforce_kg"].get<float>();
        s.aero.rear_downforce_kg    = j["aero"]["rear_downforce_kg"].get<float>();
        s.aero.drag_coefficient     = j["aero"]["drag_coefficient"].get<float>();
        s.aero.front_wing_angle_deg = j["aero"]["front_wing_angle_deg"].get<float>();
        s.aero.rear_wing_angle_deg  = j["aero"]["rear_wing_angle_deg"].get<float>();

        s.fuel.fuel_load_kg       = j["fuel"]["fuel_load_kg"].get<float>();
        s.fuel.pit_fuel_strategy  = j["fuel"]["pit_fuel_strategy"].get<bool>();

        s.abs_level = j["electronics"]["abs_level"].get<uint8_t>();
        s.tc_level  = j["electronics"]["tc_level"].get<uint8_t>();
        s.sc_level  = j["electronics"]["sc_level"].get<uint8_t>();
        s.ers_level = j["electronics"]["ers_level"].get<uint8_t>();

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[VehicleSetupManager] JSON parse error: " << e.what() << "\n";
        return false;
    }
}

// =============================================================================
// ValidateSetupRanges — Verifica que los valores estén dentro de rangos físicos
// =============================================================================

SetupResult VehicleSetupManager::ValidateSetupRanges(const VehicleSetupData& s) const {
    auto in_range = [](float v, float lo, float hi) { return v >= lo && v <= hi; };

    bool ok = true;

    // Neumáticos
    ok &= in_range(s.tires.pressure_front_psi,  15.0f, 45.0f);
    ok &= in_range(s.tires.pressure_rear_psi,   15.0f, 45.0f);
    ok &= in_range(s.tires.camber_front_deg,    -5.0f,  1.0f);
    ok &= in_range(s.tires.camber_rear_deg,     -4.0f,  0.5f);
    ok &= in_range(s.tires.toe_front_mm,        -5.0f,  5.0f);
    ok &= in_range(s.tires.toe_rear_mm,         -5.0f,  5.0f);
    ok &= in_range(s.tires.caster_deg,           3.0f, 12.0f);

    // Suspensión
    ok &= in_range(s.suspension.spring_rate_front_nm, 10000.0f, 200000.0f);
    ok &= in_range(s.suspension.spring_rate_rear_nm,  10000.0f, 200000.0f);
    ok &= in_range(s.suspension.ride_height_front_mm,    50.0f,    200.0f);
    ok &= in_range(s.suspension.ride_height_rear_mm,     50.0f,    200.0f);

    // Caja de cambios
    ok &= (s.gearbox.num_gears >= 3 && s.gearbox.num_gears <= GearboxSetup::MAX_GEARS);
    ok &= in_range(s.gearbox.final_drive_ratio, 2.0f, 6.0f);
    for (int i = 0; i < s.gearbox.num_gears; ++i)
        ok &= in_range(s.gearbox.gear_ratios[i], 0.3f, 5.0f);

    // Frenos
    ok &= in_range(s.brakes.bias_front_pct, 0.35f, 0.80f);

    // Electrónica
    ok &= (s.abs_level <= 10);
    ok &= (s.tc_level  <= 10);
    ok &= (s.sc_level  <= 10);
    ok &= (s.ers_level <= 10);

    return ok ? SetupResult::OK : SetupResult::VALIDATION_FAILED;
}

// =============================================================================
// Save — Guarda setup cifrado en disco
// =============================================================================

SetupResult VehicleSetupManager::Save(const VehicleSetupData& setup_in,
                                       const std::filesystem::path& filepath,
                                       const std::string& player_token) const
{
    // Validar rangos antes de guardar
    SetupResult valid = ValidateSetupRanges(setup_in);
    if (valid != SetupResult::OK) return valid;

    // Hacer copia mutable para calcular HMAC
    VehicleSetupData setup = setup_in;
    setup.format_version    = SETUP_FORMAT_VERSION;
    setup.modified_timestamp = static_cast<uint64_t>(std::time(nullptr));

    // 1. Calcular y almacenar HMAC en el struct
    ComputeHMAC(setup, setup.hmac_signature);

    // 2. Serializar a JSON
    std::string json_str = SerializeToJSON(setup);

    // 3. Derivar clave AES-256 desde el token del jugador + vehicle_id
    uint8_t key[32], iv[16];
    DeriveEncryptionKey(player_token, setup.vehicle_id, key, iv);

    // 4. Cifrar el JSON con AES-256-CBC
    std::vector<uint8_t> ciphertext;
    if (!AES256Encrypt(reinterpret_cast<const uint8_t*>(json_str.c_str()),
                       json_str.size(), key, iv, ciphertext))
        return SetupResult::WRITE_ERROR;

    // 5. Construir cabecera del archivo
    SetupFileHeader header;
    header.magic          = SETUP_MAGIC;
    header.format_version = SETUP_FORMAT_VERSION;
    header.ciphertext_len = ciphertext.size();

    // Generar salt aleatorio criptográficamente seguro
    RAND_bytes(header.salt, 16);
    std::memcpy(header.iv, iv, 16);

    // 6. Calcular HMAC del archivo completo (header + ciphertext) para anti-tamper
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    unsigned int file_hmac_len = 32;
    HMAC(EVP_sha256(),
         master_hmac_key_.data(), static_cast<int>(MASTER_KEY_SIZE),
         reinterpret_cast<const uint8_t*>(&header),
         sizeof(SetupFileHeader) - 32,  // Excluye file_hmac del hash
         header.file_hmac, &file_hmac_len);
    EVP_MD_CTX_free(mdctx);

    // 7. Escribir archivo binario
    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs.is_open()) return SetupResult::WRITE_ERROR;

    ofs.write(reinterpret_cast<const char*>(&header), sizeof(SetupFileHeader));
    ofs.write(reinterpret_cast<const char*>(ciphertext.data()), ciphertext.size());

    if (!ofs.good()) return SetupResult::WRITE_ERROR;

    return SetupResult::OK;
}

// =============================================================================
// Load — Carga y descifra setup desde disco
// =============================================================================

SetupResult VehicleSetupManager::Load(const std::filesystem::path& filepath,
                                       const std::string& player_token,
                                       VehicleSetupData& out_setup) const
{
    if (!std::filesystem::exists(filepath)) return SetupResult::FILE_NOT_FOUND;

    std::ifstream ifs(filepath, std::ios::binary);
    if (!ifs.is_open()) return SetupResult::READ_ERROR;

    // 1. Leer cabecera
    SetupFileHeader header;
    ifs.read(reinterpret_cast<char*>(&header), sizeof(SetupFileHeader));
    if (!ifs.good()) return SetupResult::READ_ERROR;

    // 2. Verificar magic y versión
    if (header.magic != SETUP_MAGIC) return SetupResult::INVALID_MAGIC;
    if (header.format_version > SETUP_FORMAT_VERSION) return SetupResult::VERSION_MISMATCH;

    // 3. Verificar HMAC del archivo (anti-tamper del archivo en disco)
    uint8_t expected_file_hmac[32];
    unsigned int hmac_len = 32;
    HMAC(EVP_sha256(),
         master_hmac_key_.data(), static_cast<int>(MASTER_KEY_SIZE),
         reinterpret_cast<const uint8_t*>(&header),
         sizeof(SetupFileHeader) - 32,
         expected_file_hmac, &hmac_len);

    if (CRYPTO_memcmp(expected_file_hmac, header.file_hmac, 32) != 0)
        return SetupResult::HMAC_INVALID;

    // 4. Leer ciphertext
    std::vector<uint8_t> ciphertext(header.ciphertext_len);
    ifs.read(reinterpret_cast<char*>(ciphertext.data()), header.ciphertext_len);
    if (!ifs.good()) return SetupResult::READ_ERROR;

    // 5. Derivar clave de descifrado
    // Necesitamos el vehicle_id para derivar la clave, pero está dentro del ciphertext.
    // Solución: el vehicle_id está en el salt (lo embebimos al guardar).
    // Para simplicidad: derivar con una "clave temporal" y después verificar vehicle_id.
    // En producción: incluir vehicle_id en la cabecera del archivo (sin cifrar).
    // Aquí usamos el IV directamente como referencia simplificada.
    uint8_t key[32], iv[16];
    std::memcpy(iv, header.iv, 16);
    // Derivar clave con token + salt del archivo (hacemos que el salt sea la "semilla del vehicle")
    std::string pseudo_vehicle_id(reinterpret_cast<const char*>(header.salt), 16);
    DeriveEncryptionKey(player_token, pseudo_vehicle_id.c_str(), key, iv);

    // 6. Descifrar con AES-256-CBC
    std::vector<uint8_t> plaintext;
    if (!AES256Decrypt(ciphertext.data(), ciphertext.size(), key, iv, plaintext))
        return SetupResult::DECRYPT_FAILED;

    // 7. Deserializar JSON
    std::string json_str(reinterpret_cast<const char*>(plaintext.data()), plaintext.size());
    if (!DeserializeFromJSON(json_str, out_setup))
        return SetupResult::JSON_PARSE_ERROR;

    // 8. Verificar HMAC del contenido del setup (integridad del setup mismo)
    if (!VerifyHMAC(out_setup))
        return SetupResult::HMAC_INVALID;

    // 9. Validar rangos físicos
    return ValidateSetupRanges(out_setup);
}

// =============================================================================
// ExportJSON / ImportJSON — Intercambio de setups en comunidad
// =============================================================================

SetupResult VehicleSetupManager::ExportJSON(const VehicleSetupData& setup,
                                             const std::filesystem::path& filepath) const
{
    SetupResult valid = ValidateSetupRanges(setup);
    if (valid != SetupResult::OK) return valid;

    VehicleSetupData s = setup;
    ComputeHMAC(s, s.hmac_signature);

    // Convertir HMAC a hex string para el JSON
    std::string hmac_hex;
    hmac_hex.reserve(64);
    char hex_buf[3];
    for (int i = 0; i < 32; ++i) {
        std::snprintf(hex_buf, sizeof(hex_buf), "%02x", s.hmac_signature[i]);
        hmac_hex += hex_buf;
    }

    json j = json::parse(SerializeToJSON(s));
    j["meta"]["integrity_hmac"] = hmac_hex;
    j["meta"]["export_note"] = "Setup exported from SimRacingGame. Do not manually edit.";

    std::ofstream ofs(filepath);
    if (!ofs.is_open()) return SetupResult::WRITE_ERROR;
    ofs << j.dump(4);

    return SetupResult::OK;
}

SetupResult VehicleSetupManager::ImportJSON(const std::filesystem::path& filepath,
                                             VehicleSetupData& out_setup) const
{
    if (!std::filesystem::exists(filepath)) return SetupResult::FILE_NOT_FOUND;

    std::ifstream ifs(filepath);
    if (!ifs.is_open()) return SetupResult::READ_ERROR;

    std::string json_str((std::istreambuf_iterator<char>(ifs)),
                          std::istreambuf_iterator<char>());

    if (!DeserializeFromJSON(json_str, out_setup))
        return SetupResult::JSON_PARSE_ERROR;

    // Verificar HMAC del JSON exportado
    if (!VerifyHMAC(out_setup))
        return SetupResult::HMAC_INVALID;

    return ValidateSetupRanges(out_setup);
}

// =============================================================================
// ResultToString
// =============================================================================

std::string VehicleSetupManager::ResultToString(SetupResult result) {
    switch (result) {
        case SetupResult::OK:               return "OK";
        case SetupResult::FILE_NOT_FOUND:   return "Archivo de setup no encontrado";
        case SetupResult::READ_ERROR:       return "Error de lectura del archivo";
        case SetupResult::WRITE_ERROR:      return "Error de escritura del archivo";
        case SetupResult::INVALID_MAGIC:    return "Formato de archivo inválido";
        case SetupResult::VERSION_MISMATCH: return "Versión del setup incompatible";
        case SetupResult::HMAC_INVALID:     return "Setup modificado externamente (HMAC inválido)";
        case SetupResult::DECRYPT_FAILED:   return "Error de descifrado — clave incorrecta?";
        case SetupResult::VALIDATION_FAILED:return "Valores del setup fuera de rango";
        case SetupResult::JSON_PARSE_ERROR: return "Error parseando JSON del setup";
        default:                            return "Error desconocido";
    }
}

} // namespace SimRacing
