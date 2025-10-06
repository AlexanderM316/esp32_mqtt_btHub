#ifndef common_H
#define common_H

#include "esp_log.h"

#define GATTC_TAG "esp32_mqtt_btHub"
// --- MQTT Configuration ---
#define MQTT_BROKER_URL "mqtt://" 
#define MQTT_HA_DISCOVERY_PREFIX ""
#define MQTT_USERNAME   ""   
#define MQTT_PASSWORD   ""    
// ---WiFi-Configuration ---
#define WIFI_SSID      ""
#define WIFI_PASS      ""
// --Bluetooth-Configuration--
#define REMOTE_DEVICE_NAME "Flood Light" //bluetooth device name 
#endif // common_H