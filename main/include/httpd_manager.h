#ifndef httpd_manager_H
#define httpd_manager_H

/**
 * @brief Type for Wi-Fi credential save callback
 */
typedef void (*wifi_credentials_cb_t)(const char *ssid, const char *pass);
/**
 * @brief Type for MQTT config save callback
 */
typedef void (*mqtt_config_cb_t)(const char *broker, const char *prefix, const char *user, const char *pass);
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
    mqtt_config_cb_t mqtt_config);

#endif //httpd_manager_H