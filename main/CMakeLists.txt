idf_component_register(SRCS "esp32_mqtt_btHub.c" "wifi_manager.c" "device_manager.c" "mqtt_manager.c" "httpd_manager.c" "dns_server.c" "system_metrics.c"
                    PRIV_REQUIRES bt nvs_flash mqtt json esp_http_server littlefs button 
                    INCLUDE_DIRS "." "include")
littlefs_create_partition_image(web web FLASH_IN_PROJECT)
