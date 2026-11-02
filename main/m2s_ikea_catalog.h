#pragma once
#include <stdint.h>

// IKEA's published Matter VID/PID catalogue. Only sensing devices are handled
// here; lights, plugs, remotes and thermostats require command/actuator support.
enum class M2sIkeaSensor : uint8_t {
    Unknown = 0, Motion, AirQuality, ThermHygro, WaterLeak, Contact
};

constexpr uint16_t kIkeaMatterVendorId = 4476; // 0x117C

constexpr M2sIkeaSensor m2s_ikea_sensor(uint16_t vid, uint16_t pid) {
    if (vid != kIkeaMatterVendorId) return M2sIkeaSensor::Unknown;
    switch (pid) {
    case 12288: return M2sIkeaSensor::Motion;       // MYGGSPRAY
    case 12289: return M2sIkeaSensor::AirQuality;   // ALPSTUGA
    case 32773: return M2sIkeaSensor::ThermHygro;  // TIMMERFLOTTE
    case 32774: return M2sIkeaSensor::WaterLeak;    // KLIPPBOK
    case 32775: return M2sIkeaSensor::Contact;      // MYGGBETT
    default: return M2sIkeaSensor::Unknown;
    }
}

constexpr const char *m2s_ikea_sensor_name(M2sIkeaSensor sensor) {
    switch (sensor) {
    case M2sIkeaSensor::Motion: return "MYGGSPRAY";
    case M2sIkeaSensor::AirQuality: return "ALPSTUGA";
    case M2sIkeaSensor::ThermHygro: return "TIMMERFLOTTE";
    case M2sIkeaSensor::WaterLeak: return "KLIPPBOK";
    case M2sIkeaSensor::Contact: return "MYGGBETT";
    default: return "NIEZNANY / NIEOBSLUGIWANY";
    }
}
