#ifndef httpd_manager_H
#define httpd_manager_H

#include <stdint.h>

/**
 * @brief Type for Wi-Fi credential save callback
 */
typedef void (*wifi_credentials_cb_t)(const char *ssid, const char *pass);
/**
 * @brief Type for MQTT config save callback
 */
typedef void (*mqtt_config_cb_t)(const char *broker, const char *prefix, const char *user, const char *pass);
/**
 * @brief Type for ble(gatt) config save callback
 */
typedef void (*ble_config_cb_t)(const char *device_name, const uint8_t *tx_power, const uint8_t *interval, const uint8_t *duration);
/**
 * @brief Getter callback for BLE config
 */
typedef void (*ble_get_config_cb_t)(char *device_name, uint8_t *tx_power, uint8_t *interval, uint8_t *duration);
/**
 * @brief Getter callback for BLE metrics
 */
typedef void (*ble_get_metrics_cb_t)(uint8_t *discovered_count, uint8_t *conn_count);
/**
 * @brief Start the HTTP server.
 * @param captive_portal  true for AP/captive portal mode
 */
void httpd_manager_start(bool captive_portal);
/**
 * @brief httpd manager set callbacks
 */
void httpd_manager_set_callbacks(
    wifi_credentials_cb_t  wifi_credentials,
    mqtt_config_cb_t mqtt_config,
    ble_config_cb_t ble_config,
    ble_get_config_cb_t ble_get_config,
    ble_get_metrics_cb_t ble_get_metrics);
#endif //httpd_manager_H