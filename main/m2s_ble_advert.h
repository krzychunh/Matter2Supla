#pragma once

#include <cstddef>
#include <cstdint>

// Extracts public Matter commissioning metadata from BLE AD structures.
// This parser intentionally does not extract or infer a pairing passcode.
struct m2s_ble_advert_t {
    uint16_t discriminator;
    uint16_t vendor_id;
    uint16_t product_id;
};

inline bool m2s_parse_matter_ble_ad(const uint8_t *bytes, size_t length,
                                    m2s_ble_advert_t *out)
{
    if (!bytes || !out) return false;
    size_t offset = 0;
    while (offset < length) {
        const size_t field_length = bytes[offset];
        if (!field_length) break;
        if (field_length > length - offset - 1) break;
        if (field_length >= 10 && bytes[offset + 1] == 0x16) {
            const uint8_t *data = bytes + offset + 2;
            if (data[0] == 0xF6 && data[1] == 0xFF && data[2] == 0x00) {
                const uint16_t value = static_cast<uint16_t>(data[3]) |
                    (static_cast<uint16_t>(data[4]) << 8);
                if ((value >> 12) != 0) return false;
                out->discriminator = value & 0x0FFF;
                out->vendor_id = static_cast<uint16_t>(data[5]) |
                    (static_cast<uint16_t>(data[6]) << 8);
                out->product_id = static_cast<uint16_t>(data[7]) |
                    (static_cast<uint16_t>(data[8]) << 8);
                return true;
            }
        }
        offset += field_length + 1;
    }
    return false;
}
