#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
#define M2S_DEVICE_LIMIT 16
typedef struct { uint32_t node; char name[49]; int state; } m2s_device_t;
// State: 0 saved/unknown, 1 pairing, 2 paired, 3 failed.
esp_err_t m2s_devices_init(void);
esp_err_t m2s_devices_remove(uint32_t node);
size_t m2s_devices_snapshot(m2s_device_t *out,size_t capacity);
esp_err_t m2s_devices_add(const char *name,uint32_t *node);
esp_err_t m2s_devices_adopt(const char *name,uint32_t node);
void m2s_devices_pair_finished(uint32_t node,int state);
#ifdef __cplusplus
}
#endif
