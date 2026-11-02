#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    bool connected, ap_active, testing, configured, backbone_ready;
    int last_error;
    char ssid[33], ap_ssid[33], ipv4[16], state[32];
} m2s_network_status_t;
esp_err_t m2s_network_start(void);
esp_err_t m2s_network_apply(const char *ssid, const char *password);
void m2s_network_status(m2s_network_status_t *out);
typedef struct { char ssid[33]; int rssi; int secure; } m2s_wifi_ap_t;
esp_err_t m2s_network_scan(void);
int m2s_network_scan_results(m2s_wifi_ap_t *out,int capacity,bool *busy);
esp_err_t m2s_network_factory_reset(void);
#ifdef __cplusplus
}
#endif
