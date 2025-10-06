#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "common.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"

#include "esp_system.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_LOGI(GATTC_TAG, "Initializing Wi-Fi...");
    wifi_init();

    ESP_LOGI(GATTC_TAG, "Initializing MQTT...");
    mqtt_start();

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "Controller init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "Controller enable failed: %s", esp_err_to_name(ret));
        return;
    }

    esp_bluedroid_config_t cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&cfg);
    if (ret) {
        ESP_LOGE(GATTC_TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(GATTC_TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return;
    }

    device_manager_init();

    // Register callbacks
    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret){
        ESP_LOGE(GATTC_TAG, "GAP register error: %x", ret);
        return;
    }

    ret = esp_ble_gattc_register_callback(esp_gattc_cb);
    if(ret){
        ESP_LOGE(GATTC_TAG, "GATTC register error: %x", ret);
        return;
    }

    // Register all devices (each gets its own profile)
    for (int i = 0; i < MAX_DEVICES; i++) {
        ret = esp_ble_gattc_app_register(i);
        if (ret){
            ESP_LOGE(GATTC_TAG, "Device %d register error: %x", i, ret);
        } else {
            ESP_LOGI(GATTC_TAG, "Registered device/profile %d", i);
        }
    }
    
    ret = esp_ble_gatt_set_local_mtu(200);
    if (ret){
        ESP_LOGE(GATTC_TAG, "MTU set failed: %x", ret);
    }
    // Register the MQTT callback
    device_manager_set_callbacks(mqtt_device_found, NULL, NULL, NULL,mqtt_device_state);

    // Start discovery
    ESP_LOGI(GATTC_TAG, "Starting device discovery...");
    start_device_discovery();
    
}