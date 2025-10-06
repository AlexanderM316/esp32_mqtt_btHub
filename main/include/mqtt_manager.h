#ifndef mqtt_manager_H
#define mqtt_manager_H

#include "device_manager.h" 

#include "esp_mac.h"

/**
 * @brief MQTT initilization
 */
void mqtt_start(void);
/**
 * @brief report to MQTT if a new device is found
 * @param index app id
 * @param mac address of new device
 */
void mqtt_device_found(int device_index, esp_bd_addr_t mac); 
/**
 * @brief report to MQTT if a device power state is changed (on/off)
 * @param index app id
 * @param power_state 0/1 (off/on)
 * @param mac address of new device
 */
void mqtt_device_state(int index, bool power_state, esp_bd_addr_t mac);

#endif // mqtt_manager_H