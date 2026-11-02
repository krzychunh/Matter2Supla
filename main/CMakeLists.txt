idf_component_register(SRCS "esp_ot_br.c" "m2s_bridge.cpp" "m2s_web.cpp" "m2s_readings.cpp" "m2s_network.cpp" "m2s_devices.cpp" "m2s_matter_controller.cpp" "m2s_matter_console.cpp" "m2s_ble_scan.cpp"
                       PRIV_REQUIRES m2s_supla json console esp_http_server esp_netif esp_psram openthread esp_coex esp_wifi nvs_flash bt esp_matter esp_matter_controller
                       INCLUDE_DIRS ".")
