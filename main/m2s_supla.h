#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
 bool enabled, configured, online, restart_required;
 uint32_t node;
 uint16_t temperature_endpoint, humidity_endpoint;
 int status;
 char server[128], email[128], message[96];
} m2s_supla_status_t;
// Channel 0 always remains the original TIMMERFLOTTE thermometer/hygrometer.
// New channels are appended and their type/node/endpoints are immutable.
// SUPLA protocol limit is 128 channels total: channel 0 + up to 127 extras.
// Actual capacity is additionally bounded by Matter registry, memory and NVS.
#define M2S_SUPLA_EXTRA_LIMIT 127
typedef enum {
 M2S_SUPLA_SENSOR_TH=1,
 M2S_SUPLA_SENSOR_MOTION=2,
 M2S_SUPLA_SENSOR_CONTACT=3,
 M2S_SUPLA_SENSOR_LEAK=4,
 M2S_SUPLA_SENSOR_CO2=5,
 M2S_SUPLA_SENSOR_PM25=6
} m2s_supla_sensor_type_t;
typedef struct {
 uint32_t node;
 uint16_t endpoint;
 uint16_t aux_endpoint;
 uint8_t type;
 uint8_t invert;
} m2s_supla_sensor_slot_t;
void m2s_supla_init(void);
void m2s_supla_tick(void);
void m2s_supla_status(m2s_supla_status_t *out);
esp_err_t m2s_supla_configure(bool enabled,const char *server,const char *email,uint32_t node,uint16_t temp_ep,uint16_t hum_ep);
size_t m2s_supla_slots_snapshot(m2s_supla_sensor_slot_t *out,size_t capacity);
esp_err_t m2s_supla_add_sensor(uint32_t node,uint8_t type,uint16_t endpoint,uint16_t aux_endpoint,bool invert);
#ifdef __cplusplus
}
#endif
