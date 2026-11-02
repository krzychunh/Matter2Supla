// Matter2Supla v0.9.1: diagnostyka backbone netif i ostrozny restart Thread.
// Nie zmienia datasetu, NVS ani konfiguracji interfejsow.
#include "m2s_bridge.h"

#include <Arduino.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_border_router.h"
#include "esp_openthread_lock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <openthread/dataset.h>
#include <openthread/ip6.h>
#include <openthread/thread.h>

static const char *TAG = "M2S_v091";

static const char *role_name(otDeviceRole role)
{
    switch (role) {
    case OT_DEVICE_ROLE_DISABLED: return "DISABLED";
    case OT_DEVICE_ROLE_DETACHED: return "DETACHED";
    case OT_DEVICE_ROLE_CHILD: return "CHILD";
    case OT_DEVICE_ROLE_ROUTER: return "ROUTER";
    case OT_DEVICE_ROLE_LEADER: return "LEADER";
    default: return "UNKNOWN";
    }
}

static void log_backbone_indices(void)
{
    esp_netif_t *backbone = esp_openthread_get_backbone_netif();
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    const int backbone_index = backbone ? esp_netif_get_netif_impl_index(backbone) : -1;
    const int sta_index = sta ? esp_netif_get_netif_impl_index(sta) : -1;

    ESP_LOGI(TAG, "M2S_BACKBONE_INDEX=%d M2S_WIFI_STA_INDEX=%d M2S_BACKBONE_IS_STA=%s",
             backbone_index, sta_index, (backbone && sta && backbone == sta) ? "YES" : "NO");
    if (!backbone || backbone_index <= 0) {
        ESP_LOGW(TAG, "M2S_BACKBONE=UNAVAILABLE_OR_INVALID; sprawdz kolejnosc OTBR/WiFi");
    } else if (sta && backbone != sta) {
        ESP_LOGW(TAG, "M2S_BACKBONE=NOT_WIFI_STA; sprawdz esp_openthread_set_backbone_netif");
    }
    // Porownac z 'ot br infraif' w CLI. Nie koryguj indeksu na slepo.
}

static void resume_existing_thread_once(void)
{
    if (!esp_openthread_lock_acquire(pdMS_TO_TICKS(1000))) {
        ESP_LOGW(TAG, "M2S_AUTORESUME=LOCK_BUSY");
        return;
    }
    otInstance *instance = esp_openthread_get_instance();
    if (!instance) {
        ESP_LOGW(TAG, "M2S_AUTORESUME=NO_INSTANCE");
    } else if (otThreadGetDeviceRole(instance) != OT_DEVICE_ROLE_DISABLED) {
        ESP_LOGI(TAG, "M2S_AUTORESUME=NOT_NEEDED role=%s", role_name(otThreadGetDeviceRole(instance)));
    } else if (!otDatasetIsCommissioned(instance)) {
        ESP_LOGW(TAG, "M2S_AUTORESUME=SKIPPED_NO_ACTIVE_DATASET (NVS untouched)");
    } else {
        // Uzywa wylacznie istniejacego Active Operational Dataset.
        // Nie wywoluje otDatasetSetActive*, factoryreset ani generate dataset.
        otError ip6 = otIp6SetEnabled(instance, true);
        otError thread = (ip6 == OT_ERROR_NONE || ip6 == OT_ERROR_ALREADY)
                             ? otThreadSetEnabled(instance, true) : ip6;
        ESP_LOGI(TAG, "M2S_AUTORESUME=ATTEMPT ip6_error=%d thread_error=%d",
                 (int)ip6, (int)thread);
    }
    esp_openthread_lock_release();
}

static void health_task(void *unused)
{
    (void)unused;
    // W przeslanym v0.9 nie ma wywolania ot_network_auto_start().
    // Nie wywolujemy go tutaj: bez aktywnego datasetu moglby tworzyc siec.
    // Bezpiecznie wznawiamy wylacznie zapisany, commissioned dataset.
    vTaskDelay(pdMS_TO_TICKS(15000));
    ESP_LOGI(TAG, "M2S_AUTORESUME=MANAGED_BY_NETWORK; Thread deferred during setup AP");

    for (;;) {
        const char *role = "OT_LOCK_BUSY";
        if (esp_openthread_lock_acquire(pdMS_TO_TICKS(250))) {
            otInstance *instance = esp_openthread_get_instance();
            role = instance ? role_name(otThreadGetDeviceRole(instance)) : "OT_NOT_READY";
            esp_openthread_lock_release();
        }
        log_backbone_indices();
        const size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const size_t dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
        const size_t dmaLargest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
        const size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "M2S_THREAD_ROLE=%s M2S_HEAP internal=%u KiB largest=%u KiB DMA=%u B DMA_largest=%u B psram=%u KiB",
                 role, (unsigned)(internal / 1024), (unsigned)(largest / 1024),
                 (unsigned)dma, (unsigned)dmaLargest, (unsigned)(psram / 1024));
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

extern "C" void m2s_bridge_start(void)
{
    String runtime = String("ESP-IDF + Arduino ") + String(ARDUINO);
    ESP_LOGI(TAG, "===== Matter2Supla v0.9.1 OTBR diagnostics =====");
    ESP_LOGI(TAG, "M2S_ARDUINO=LINKED (%s)", runtime.c_str());
    ESP_LOGI(TAG, "M2S_WEB=CONFIGURATION M2S_SUPLA=STAGE4_SRPC_DIAGNOSTICS");
    ESP_LOGW(TAG, "M2S_IPV6_END_TO_END=NOT_VERIFIED");
    if (xTaskCreate(health_task, "m2s_health", 4096, nullptr, 2, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "M2S_HEALTH_TASK=FAILED");
    }
}
