#ifndef device_manager_H
#define device_manager_H

#include "esp_gatt_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#define MAX_DEVICES 8  // max number of devices
#define CMD_MAX_LEN 12 //  max length of w_cmd
#define INVALID_HANDLE   0
#define REMOTE_SERVICE_UUID        0xFFA0
#define REMOTE_NOTIFY_CHAR_UUID    0xFFA2
#define REMOTE_WRITE_CHAR_UUID     0xFFA1 

// Callback function types
typedef void (*device_found_cb_t)(int index, esp_bd_addr_t mac);
typedef void (*all_devices_found_cb_t)(void);
typedef void (*device_connected_cb_t)(int device_index);
typedef void (*device_disconnected_cb_t)(int device_index);
typedef void (*device_power_state_cb_t)(int device_index, bool power_state, esp_bd_addr_t mac);

/* Single structure for each device - combines device and profile */
typedef struct {
    // Device identification
    esp_bd_addr_t mac_address;
    char name[32];
    
    // Connection state
    bool connected;

    // State reporting
    bool power_state;

    // Command queue 
    uint8_t pending_cmd[CMD_MAX_LEN];
    uint8_t pending_cmd_len;
    bool has_pending;

    // GATT profile state
    uint16_t conn_id;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t char_handle;
    uint16_t write_char_handle;  

    // App ID (index-based)
    uint16_t app_id;
} flood_light_device_t;

typedef struct {
    bool scanning;
    uint8_t scan_interval;
    uint8_t scan_duration;
    TimerHandle_t scan_timer;
    bool all_devices_found;
    uint16_t gattc_if;
    flood_light_device_t devices[MAX_DEVICES];
    uint8_t discovered_count;
    uint8_t conn_count; // number of active connections

    // Callbacks
    device_found_cb_t device_found_cb;
    all_devices_found_cb_t all_devices_found_cb;
    device_connected_cb_t device_connected_cb;
    device_disconnected_cb_t device_disconnected_cb;
    device_power_state_cb_t device_power_state_cb;
} device_manager_t;

// Global
extern device_manager_t device_manager;
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
 * @brief find app id of device with this mac
 * @param device_index mac address
 * @return device app id (int)
 */ 
int find_device_by_mac(esp_bd_addr_t mac_addr);
/**
 * @brief connect to device
 * @param device_index device app id
 */
bool connect_to_device(int device_index);
/**
 * @brief turn on bluetooth light
 * @param device_index device app id
 */
bool device_set_on(int device_index);
/**
 * @brief turn off bluetooth light
 * @param device_index device app id
 */
bool device_set_off(int device_index);
/**
 * @brief device_set_brightness set brightness of bluetooth light
 * @param device_index device app id
 * @param brightness brightness (in hex)
 */
bool device_set_brightness(int device_index, uint8_t brightness);
/**
 * @brief set color of bluetooth light
 * @param device_index device app id
 * @param r red (in hex)
 * @param g greeb (in hex)
 * @param b blue (in hex)
 */
bool device_set_color(int device_index, uint8_t r, uint8_t g ,uint8_t b);
/**
 * @brief set device config 
 * @param device_name new device name 
 * @param tx_power ble power level
 */
void ble_update_config(const char *device_name, const uint8_t *tx_power, const uint8_t *interval, const uint8_t *duration);
/**
 * @brief getter for current config
 */
void ble_get_config(char *device_name, uint8_t *tx_power, uint8_t *interval, uint8_t *duration);
/**
 * @brief getter for ble metrics
 */
void ble_get_metrics(uint8_t *discovered_count, uint8_t *conn_count);
#endif // device_manager_H
