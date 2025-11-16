#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "httpd_manager.h"
#include "device_manager.h" 

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "lwip/err.h"
#include "lwip/sys.h"

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_LOGI(TAG, "Initializing device manager...");
    device_manager_init();

    ESP_LOGI(TAG, "Initializing Wi-Fi...");
    wifi_init();
    
    ESP_LOGI(TAG, "Initializing MQTT...");
    mqtt_start();
    
    // Register the BLE callbacks
    device_manager_set_callbacks(mqtt_device_found, NULL, NULL, NULL,mqtt_device_state);
    // Register the MQTT callbacks
    mqtt_set_callbacks(device_set_power, device_set_brightness, device_set_color, ble_get_metrics, ble_get_devices);
    // Register the httpd server callbacks
    httpd_manager_set_callbacks(wifi_update_credentials, mqtt_update_config, mqtt_get_config, ble_update_config,
                                ble_get_config, ble_get_metrics, ble_get_devices, ble_reset_devices);
  
}