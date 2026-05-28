#pragma once
// =============================================================================
// NetworkTypes.h
// Estructuras de datos de red: paquetes Cliente↔Servidor para el Multiplayer Loop
// Diseñadas para ser compactas (<128 bytes por paquete típico) y extensibles.
// =============================================================================

#include <cstdint>
#include <cstring>
#include <array>

namespace SimRacing::Net {

// IDs de tipo de paquete (1 byte)
enum class PacketType : uint8_t {
    INPUT        = 0x01,   // Cliente → Servidor: input del jugador
    WORLD_SNAP   = 0x02,   // Servidor → Cliente: snapshot del mundo
    DELTA_SNAP   = 0x03,   // Servidor → Cliente: snapshot delta-comprimido
    LAP_TIME     = 0x04,   // Servidor → Cliente: tiempo de vuelta validado
    SKIN_REQUEST = 0x05,   // Cliente → Servidor: solicitar skin de otro jugador
    SKIN_DATA    = 0x06,   // Servidor → Cliente: datos de skin (por chunks)
    RACE_EVENT   = 0x07,   // Servidor → Cliente: penalty, pit, flag
    PING         = 0xFE,
    PONG         = 0xFF,
};

// =============================================================================
// InputPacket — Cliente envía al servidor cada frame de input (~48 bytes)
// =============================================================================
#pragma pack(push, 1)

struct InputPacket {
    uint8_t  packet_type    = static_cast<uint8_t>(PacketType::INPUT);
    uint64_t sequence       = 0;    // Monotónico, nunca retrocede
    uint64_t client_time_ms = 0;    // Timestamp del cliente (para lag compensation)
    float    throttle       = 0.0f; // [0.0, 1.0]
    float    brake          = 0.0f; // [0.0, 1.0]
    float    steering       = 0.0f; // [-1.0, 1.0]
    float    clutch         = 0.0f; // [0.0, 1.0]
    int8_t   gear           = 1;    // [-1=R, 0=N, 1-7=Drive]
    uint8_t  flags          = 0;    // bit0=handbrake, bit1=pit_req, bit2=reset_req
    uint8_t  _pad[2]        = {};
    uint32_t crc32          = 0;    // CRC32 del paquete (campos anteriores)
    // TOTAL: 48 bytes

    void ComputeCRC();
    bool VerifyCRC() const;
};

// =============================================================================
// CarState — Estado de un coche en el snapshot (84 bytes)
// =============================================================================

struct CarState {
    uint32_t player_id      = 0;
    uint64_t input_ack      = 0;        // Último input procesado para este coche
    float    pos_x          = 0.0f;
    float    pos_y          = 0.0f;
    float    pos_z          = 0.0f;
    float    vel_x          = 0.0f;
    float    vel_y          = 0.0f;
    float    vel_z          = 0.0f;
    float    rot_w          = 1.0f;     // Quaternion
    float    rot_x          = 0.0f;
    float    rot_y          = 0.0f;
    float    rot_z          = 0.0f;
    float    ang_vel_x      = 0.0f;
    float    ang_vel_y      = 0.0f;
    float    ang_vel_z      = 0.0f;
    float    wheel_rpm[4]   = {};       // RPM por rueda [FL, FR, RL, RR]
    uint16_t engine_rpm     = 0;        // RPM del motor
    int8_t   gear           = 1;
    uint8_t  damage_flags   = 0;        // bits: FL/FR/RL/RR ruedas, engine, aero
    // TOTAL: 84 bytes
};

// =============================================================================
// WorldSnapshot — Servidor → Clientes (cabecera + array de CarState)
// Tamaño total con 16 coches: 8 + 1 + 4 + 16*84 + 32 = ~1389 bytes
// Con delta compression → ~200-400 bytes típico
// =============================================================================

static constexpr uint8_t MAX_CARS_PER_RACE = 32;

struct WorldSnapshotHeader {
    uint8_t  packet_type  = static_cast<uint8_t>(PacketType::WORLD_SNAP);
    uint64_t server_tick  = 0;
    uint32_t race_time_ms = 0;
    uint8_t  num_cars     = 0;
    uint8_t  flags        = 0;  // bit0=race_started, bit1=safety_car, etc.
    uint8_t  _pad[2]      = {};
    // Seguido de num_cars * sizeof(CarState) bytes
    // Seguido de 32 bytes HMAC-SHA256
};

// =============================================================================
// LapTimeRecord — Tiempo de vuelta firmado por el servidor
// =============================================================================

struct LapTimeRecord {
    uint8_t  packet_type    = static_cast<uint8_t>(PacketType::LAP_TIME);
    uint32_t player_id      = 0;
    uint32_t track_id       = 0;
    uint32_t car_id         = 0;
    uint64_t lap_time_ms    = 0;    // Tiempo de vuelta en milisegundos
    uint64_t server_tick    = 0;
    uint64_t unix_timestamp = 0;
    uint8_t  replay_hash[32] = {};  // SHA256 del replay completo (para auditoría)
    uint8_t  signature[32]   = {};  // HMAC-SHA256 firmado por el servidor
    // TOTAL: ~106 bytes
};

// =============================================================================
// SkinSyncPacket — Sincronización de skins entre jugadores
// La skin se transfiere como chunks de hasta 1400 bytes (MTU seguro)
// para evitar fragmentación UDP
// =============================================================================

struct SkinRequestPacket {
    uint8_t  packet_type = static_cast<uint8_t>(PacketType::SKIN_REQUEST);
    uint32_t player_id   = 0;   // ID del jugador cuya skin queremos
    uint32_t skin_hash   = 0;   // Hash CRC32 de la skin que ya tenemos (0 = ninguna)
    // Si skin_hash coincide con la del servidor, no se envía → ahorra ancho de banda
};

static constexpr uint16_t SKIN_CHUNK_SIZE = 1400;
static constexpr uint16_t MAX_SKIN_CHUNKS = 512;  // Max: 512 * 1400 = 716 KB por skin

struct SkinDataPacket {
    uint8_t  packet_type  = static_cast<uint8_t>(PacketType::SKIN_DATA);
    uint32_t player_id    = 0;
    uint32_t skin_hash    = 0;   // Hash del skin completo (para verificar al final)
    uint16_t chunk_index  = 0;   // Índice del chunk actual
    uint16_t total_chunks = 0;   // Total de chunks
    uint16_t chunk_len    = 0;   // Longitud de datos en este chunk
    uint8_t  data[SKIN_CHUNK_SIZE] = {};
    // TOTAL: ~1415 bytes — justo bajo el MTU UDP típico de 1500 bytes
};

#pragma pack(pop)

// =============================================================================
// Utilidades de CRC32 (para verificación de paquetes)
// Implementación tabla de Castagnoli
// =============================================================================

uint32_t ComputeCRC32(const void* data, size_t len);

inline void InputPacket::ComputeCRC() {
    const size_t crc_field_offset = offsetof(InputPacket, crc32);
    std::memset(&crc32, 0, sizeof(crc32));
    crc32 = ComputeCRC32(this, crc_field_offset);
}

inline bool InputPacket::VerifyCRC() const {
    InputPacket copy = *this;
    std::memset(&copy.crc32, 0, sizeof(copy.crc32));
    const size_t crc_field_offset = offsetof(InputPacket, crc32);
    return ComputeCRC32(&copy, crc_field_offset) == crc32;
}

} // namespace SimRacing::Net
