#ifndef wifi_manager_H
#define wifi_manager_H

#include "esp_wifi.h"

/* Wi-Fi event group bits */
#define WIFI_CONNECTED_BIT BIT0  
#define WIFI_FAIL_BIT      BIT1  

/**
 * @brief WiFi initialization 
 */
void wifi_init(void);
/**
 * @brief WiFi event handler
 */
void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data);
#endif // wifi_manager_H