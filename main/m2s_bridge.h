#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Warstwa diagnostyczna Matter2Supla uruchamiana po esp_openthread_start().
// Arduino jest komponentem, ale stos Wi-Fi / Thread należy do ESP-IDF.
void m2s_bridge_start(void);

#ifdef __cplusplus
}
#endif
