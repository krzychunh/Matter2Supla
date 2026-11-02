idf_component_register(SRCS "m2s_supla.cpp"
  INCLUDE_DIRS "../../main"
  PRIV_REQUIRES supla-device esp_wifi esp_netif nvs_flash esp_timer esp_system)
