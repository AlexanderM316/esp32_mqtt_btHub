#ifndef mqtt_manager_H
#define mqtt_manager_H

#include "device_manager.h" 

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
/**
 * @brief set mqtt config from http
 * @param *broker adress of the broker
 * @param *prefix prefix for auto discovery
 * @param *user username
 * @param *pass password
 */                             
void mqtt_update_config(const char *broker, const char *prefix, const char *user, const char *pass);

#endif // mqtt_manager_H