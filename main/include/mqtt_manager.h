#ifndef mqtt_manager_H
#define mqtt_manager_H

#include <stdint.h>

/**
 * @brief MQTT initilization
 */
void mqtt_start(void);
/**
 * @brief report to MQTT if a new device is found
 * @param index app id
 * @param mac address of new device
 */
void mqtt_device_found(const uint8_t *mac, const char *name); 
/**
 * @brief report to MQTT if a device power state is changed (on/off)
 * @param index app id
 * @param power_state 0/1 (off/on)
 * @param mac address of new device
 */
void mqtt_device_state(bool power_state, uint8_t *mac);
/**
 * @brief set mqtt config from http
 * @param *broker adress of the broker
 * @param *prefix prefix for auto discovery
 * @param *user username
 * @param *pass password
 */                             
void mqtt_update_config(const char *broker, const char *prefix, const char *user, const char *pass);
/**
 * @brief callback to turn ble device on/off
 * @param mac address of device
 * @param power on/off
 */
typedef bool (*device_set_power_cb_t)(const uint8_t *mac, const bool power);
/**
 * @brief callback to set ble device brightness
 * @param mac address of device
 * @param brightness 
 */
typedef bool (*device_set_brightness_cb_t)(const uint8_t *mac, const uint8_t brightness);
/**
 * @brief callback to set ble device color
 * @param mac address of device
 * @param r red
 * @param g green
 * @param b blue
 */
typedef bool (*device_set_color_cb_t)(const uint8_t *mac, const uint8_t r,const uint8_t g,
                const uint8_t b);
/**
 * @brief getter callback for general ble metrics
 */
typedef void (*ble_get_metrics_cb_t)(uint8_t *discovered_count, uint8_t *conn_count);
/**
 * @brief getter callcack for ble devices
 */
typedef void (*ble_get_devices_cb_t)(uint8_t *indexes,const char **names, uint8_t *macs, bool *connected, uint16_t *uuids, int8_t *rssis);
/**
 * @brief set mqtt callbacks
 */
void mqtt_set_callbacks(device_set_power_cb_t device_set_power, device_set_brightness_cb_t device_set_brightness,
                        device_set_color_cb_t device_set_color, ble_get_metrics_cb_t ble_get_metrics, ble_get_devices_cb_t ble_get_devices);

#endif // mqtt_manager_H