#include "sdkconfig.h"
#include "m2s_ble_scan.h"
#include "m2s_ble_advert.h"

#include <cstring>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#if defined(CONFIG_BT_NIMBLE_ENABLED) && CONFIG_BT_NIMBLE_ENABLED && \
    defined(CONFIG_ENABLE_ESP32_BLE_CONTROLLER) && CONFIG_ENABLE_ESP32_BLE_CONTROLLER
#define M2S_HAVE_NIMBLE 1
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#else
#define M2S_HAVE_NIMBLE 0
#endif

namespace {
constexpr int32_t kScanDurationMs = 8000;
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
m2s_ble_scan_snapshot_t s_state{};
int64_t s_started_us = 0;

#if M2S_HAVE_NIMBLE
int gap_event(ble_gap_event *event, void *)
{
    if (!event) return 0;
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        portENTER_CRITICAL(&s_lock);
        s_state.active = false;
        const int reason = event->disc_complete.reason;
        s_state.host_error = (reason == 0 || reason == BLE_HS_ETIMEOUT) ? 0 : reason;
        portEXIT_CRITICAL(&s_lock);
        return 0;
    }
    if (event->type != BLE_GAP_EVENT_DISC) return 0;

    m2s_ble_scan_item_t item{};
    const auto &disc = event->disc;
    m2s_ble_advert_t advert{};
    if (!m2s_parse_matter_ble_ad(disc.data, disc.length_data, &advert)) return 0;
    item.discriminator = advert.discriminator;
    item.vendor_id = advert.vendor_id;
    item.product_id = advert.product_id;
    std::memcpy(item.address, disc.addr.val, sizeof(item.address));
    item.address_type = disc.addr.type;
    item.rssi = disc.rssi;

    portENTER_CRITICAL(&s_lock);
    if (s_state.active) {
        bool known = false;
        for (unsigned i = 0; i < s_state.count; i++) {
            auto &previous = s_state.items[i];
            if (previous.address_type == item.address_type &&
                std::memcmp(previous.address, item.address, sizeof(item.address)) == 0) {
                previous = item;
                known = true;
                break;
            }
        }
        if (!known && s_state.count < M2S_BLE_SCAN_LIMIT) {
            s_state.items[s_state.count++] = item;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return 0;
}

void start_failed(int host_rc)
{
    portENTER_CRITICAL(&s_lock);
    s_state.active = false;
    s_state.host_error = host_rc;
    portEXIT_CRITICAL(&s_lock);
}
#endif
} // namespace

esp_err_t m2s_ble_scan_start()
{
#if !M2S_HAVE_NIMBLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    // The Matter controller owns NimBLE. Never initialize it a second time.
    if (!ble_hs_synced() || ble_gap_disc_active()) return ESP_ERR_INVALID_STATE;
    uint8_t own_address_type = 0;
    const int address_rc = ble_hs_id_infer_auto(0, &own_address_type);
    if (address_rc != 0) return ESP_ERR_INVALID_STATE;

    portENTER_CRITICAL(&s_lock);
    const bool busy = s_state.active;
    if (!busy) {
        s_state = {};
        s_state.available = true;
        s_state.active = true;
        s_started_us = esp_timer_get_time();
    }
    portEXIT_CRITICAL(&s_lock);
    if (busy) return ESP_ERR_INVALID_STATE;

    ble_gap_disc_params params{};
    params.passive = 1; // Do not request additional data from nearby devices.
    params.filter_duplicates = 1;
    params.itvl = 0x80;  // 80 ms; leaves time for Wi-Fi and Thread.
    params.window = 0x20; // 20 ms.
    const int rc = ble_gap_disc(own_address_type, kScanDurationMs, &params,
                                gap_event, nullptr);
    if (rc != 0) {
        start_failed(rc);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
#endif
}

esp_err_t m2s_ble_scan_stop()
{
#if !M2S_HAVE_NIMBLE
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!m2s_ble_scan_active()) return ESP_ERR_INVALID_STATE;
    const int rc = ble_gap_disc_cancel();
    if (rc != 0 && rc != BLE_HS_EALREADY) return ESP_FAIL;
    portENTER_CRITICAL(&s_lock);
    s_state.active = false;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
#endif
}

bool m2s_ble_scan_active()
{
    portENTER_CRITICAL(&s_lock);
    const bool active = s_state.active;
    portEXIT_CRITICAL(&s_lock);
    return active;
}

void m2s_ble_scan_snapshot(m2s_ble_scan_snapshot_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_lock);
    *out = s_state;
    const int64_t started = s_started_us;
    portEXIT_CRITICAL(&s_lock);
    out->available = M2S_HAVE_NIMBLE;
    if (started > 0) {
        const int64_t elapsed = (esp_timer_get_time() - started) / 1000;
        out->elapsed_ms = static_cast<uint32_t>(elapsed > 0 ? elapsed : 0);
    }
}
