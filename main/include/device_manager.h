#ifndef device_manager_H
#define device_manager_H

#include <stdint.h>

// Callback function types
typedef void (*device_found_cb_t)(const uint8_t *mac, const char *name);
typedef void (*all_devices_found_cb_t)(void);
typedef void (*device_connected_cb_t)(int device_index);
typedef void (*device_disconnected_cb_t)(int device_index);
typedef void (*device_power_state_cb_t)(bool power_state, uint8_t *mac);

/**
 * @brief device manager initialization 
 */
void device_manager_init(void);
/**
 * @brief device manager set callbacks
 */
void device_manager_set_callbacks(
    device_found_cb_t device_found, 
    all_devices_found_cb_t all_found,
    device_connected_cb_t device_connected,
    device_disconnected_cb_t device_disconnected,
    device_power_state_cb_t device_power_state);

/**
 * @brief connect to device
 * @param device_index device app id
 */
bool connect_to_device(int device_index);
/**
 * @brief turn ble device on/off
 * @param mac address of device
 * @param power on/off
 */
bool device_set_power(const uint8_t *mac, const bool power);
/**
 * @brief device_set_brightness set brightness of bluetooth light
 * @param mac address of device
 * @param brightness brightness (in hex)
 */
bool device_set_brightness(const uint8_t *mac, uint8_t brightness);
/**
 * @brief set color of bluetooth light
 * @param mac address of device
 * @param r red (in hex)
 * @param g greeb (in hex)
 * @param b blue (in hex)
 */
bool device_set_color(const uint8_t *mac, uint8_t r, uint8_t g ,uint8_t b);
/**
 * @brief reset device list
 */
bool ble_reset_devices(void);
/**
 * @brief set device config 
 * @param by_name enable filter by name?
 * @param device_name new device name filter
 * @param by_uuid enable filter by UUID?
 * @param uuid new uuid filter
 * @param tx_power ble power level
 * @param interval time between scans in s
 * @param druation scan duration
 * @param mtu mtu size
 */
void ble_update_config( const bool *by_name, const char *device_name, const bool *by_uuid, const uint16_t *uuid,
                        const uint8_t *tx_power, const uint8_t *interval, const uint8_t *duration, const uint16_t *mtu);
/**
 * @brief getter for current config
 */
void ble_get_config(bool *by_name, char *device_name, bool *by_uuid, uint16_t *uuid, 
                    uint8_t *tx_power, uint8_t *interval, uint8_t *duration, uint16_t *mtu);
/**
 * @brief getter for general ble metrics
 */
void ble_get_metrics(uint8_t *discovered_count, uint8_t *conn_count);
/**
 * @brief getter for ble devices
 */
void ble_get_devices(uint8_t *indexes,const char **names, uint8_t *macs, bool *connected, uint16_t *uuids, int8_t *rssi);
#endif // device_manager_H
