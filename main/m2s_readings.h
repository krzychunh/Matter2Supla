#pragma once
#include <stdint.h>
#include "esp_err.h"
struct m2s_measurement_t { uint16_t endpoint; uint32_t cluster; int value; bool valid; int64_t received_us; int64_t changed_us; };
struct m2s_readings_t {
    uint32_t node;
    bool busy;
    int state; // 0 idle, 1 pending, 2 complete, 3 error, 4 timed out (awaiting SDK cleanup)
    int count;
    int64_t started_us;
    m2s_measurement_t values[8];
    // Matter Power Source 0x002F/0x000C: raw 0..200, units of 0.5%.
    // Volatile cache only; NVS format and Matter fabrics are unchanged.
    uint8_t battery_raw;
    bool battery_valid;
    int64_t battery_received_us;
    // BasicInformation cluster 0x0028, VID=attr2, PID=attr4, endpoint0.
    // Runtime cache, never persisted over the existing NVS registry.
    uint16_t vendor_id;
    uint16_t product_id;
    bool vendor_valid;
    bool product_valid;
};
esp_err_t m2s_readings_request(uint32_t node);
void m2s_readings_snapshot(m2s_readings_t *out);

extern "C" void m2s_readings_init(void);
void m2s_readings_tick();
bool m2s_readings_cached(uint32_t node,m2s_readings_t *out);

// Configurable automatic read polling per Matter node (Stage 9).
// NVS namespace m2s_poll9 (unchanged); default 1 min, allowed 1..1440 min.
// received_us is the last Matter report reception, changed_us the last distinct value;
// neither proves when the physical sensor sampled its environment.
uint16_t m2s_readings_interval_minutes(uint32_t node);
esp_err_t m2s_readings_set_interval(uint32_t node, uint16_t minutes);

// Volatile per-node Matter subscription diagnostics; exposed by /api/status.
struct m2s_subscription_t {
    bool requested;
    bool reporting;
    int64_t requested_us;
    int64_t report_us;
    uint32_t report_count;
};
bool m2s_readings_subscription(uint32_t node,m2s_subscription_t *out);
