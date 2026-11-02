#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

// A bounded, volatile inventory of Matter commissioning advertisements.
// No pairing, credentials, NVS, BLE-host initialization or background scans.
constexpr size_t M2S_BLE_SCAN_LIMIT = 12;

struct m2s_ble_scan_item_t {
    uint8_t address[6]; // Used for de-duplication; not exposed by the HTTP API.
    uint8_t address_type;
    int8_t rssi;
    uint16_t discriminator;
    uint16_t vendor_id;
    uint16_t product_id;
};

struct m2s_ble_scan_snapshot_t {
    bool available;
    bool active;
    uint16_t count;
    int host_error;
    uint32_t elapsed_ms;
    m2s_ble_scan_item_t items[M2S_BLE_SCAN_LIMIT];
};

esp_err_t m2s_ble_scan_start();
esp_err_t m2s_ble_scan_stop();
bool m2s_ble_scan_active();
void m2s_ble_scan_snapshot(m2s_ble_scan_snapshot_t *out);
