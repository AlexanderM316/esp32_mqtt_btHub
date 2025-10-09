idf_component_register(SRCS "esp32_mqtt_btHub.c" "wifi_manager.c" "device_manager.c" "mqtt_manager.c"
                    PRIV_REQUIRES bt nvs_flash mqtt json
                    INCLUDE_DIRS "." "include")
