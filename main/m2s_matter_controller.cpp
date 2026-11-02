#include "m2s_matter_controller.h"

#include <esp_matter.h>
#include <esp_matter_controller_client.h>
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_openthread_border_router.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "m2s_ikea_paa.inc"

static void m2s_ble_heap_snapshot(const char *stage)
{
#if CONFIG_ENABLE_ESP32_BLE_CONTROLLER
    const unsigned internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const unsigned dma = internal | MALLOC_CAP_DMA;
    const unsigned psram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    ESP_LOGI("M2S_BLE_HEAP",
             "%s: internal_free=%zu internal_largest=%zu "
             "dma_free=%zu dma_largest=%zu psram_free=%zu",
             stage,
             heap_caps_get_free_size(internal),
             heap_caps_get_largest_free_block(internal),
             heap_caps_get_free_size(dma),
             heap_caps_get_largest_free_block(dma),
             heap_caps_get_free_size(psram));
#else
    (void) stage;
#endif
}

// Compile-only integration probe: does not initialize Matter or OpenThread.
extern "C" int m2s_matter_controller_probe(void)
{
    return esp_matter::is_started() ? 1 : 0;
}

// Compiled but never called while CONFIG_M2S_MATTER_CONTROLLER_AUTOSTART is off.
extern "C" int m2s_matter_controller_start_experimental(void)
{
    if (esp_matter::is_started()) {
        return ESP_ERR_INVALID_STATE;
    }
    bool ready = false;
    for (int attempt = 0; attempt < 450; ++attempt) {
        esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        wifi_ap_record_t ap = {};
        esp_netif_ip_info_t ip = {};
        if (sta && esp_openthread_get_backbone_netif() == sta &&
            esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
            esp_netif_get_ip_info(sta, &ip) == ESP_OK &&
            ip.ip.addr != 0) {
            ready = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (!ready) {
        return ESP_ERR_TIMEOUT;
    }
    m2s_ble_heap_snapshot("before_matter_start");
    esp_err_t err = esp_matter::start(nullptr);
    m2s_ble_heap_snapshot("after_matter_start");
    if (err != ESP_OK) {
        return err;
    }
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    auto &controller = esp_matter::controller::matter_controller_client::get_instance();
    m2s_ble_heap_snapshot("before_controller_init");
    err = controller.init(112233, 1, 5580);
    m2s_ble_heap_snapshot("after_controller_init");
    if (err != ESP_OK) {
        return err;
    }
    m2s_ble_heap_snapshot("before_setup_commissioner");
#if CONFIG_CUSTOM_ATTESTATION_TRUST_STORE
    m2s_install_ikea_paa();
#endif
    err = controller.setup_commissioner();
    m2s_ble_heap_snapshot("after_setup_commissioner");
    return err;
}
