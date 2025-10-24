#include "wifi_manager.h"
#include "httpd_manager.h"

#include "nvs.h"
#include "esp_log.h"
#include "mdns.h"

#define WIFI_NAMESPACE "wifi"

static const char *TAG = "WIFI";

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 5; // number of retries before we switch to AP fallback
static int s_current_retry = 0; // retry counter

/**
 * @brief load wifi credentails from nvs 
*/
static esp_err_t wifi_load_credentials(char* ssid, size_t ssid_len, char* pass, size_t pass_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    err = nvs_get_str(handle, "ssid", ssid, &ssid_len);
    if (err != ESP_OK) { nvs_close(handle); return err; }

    err = nvs_get_str(handle, "pass", pass, &pass_len);
    nvs_close(handle);
    return err;
}
/**
 * @brief save wifi credentails to nvs 
*/
static esp_err_t wifi_save_credentials(const char* ssid, const char* pass)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    nvs_set_str(handle, "ssid", ssid);
    nvs_set_str(handle, "pass", pass);
    nvs_commit(handle);
    nvs_close(handle);
    return ESP_OK;
}
/**
 * @brief start WiFi access point
*/
static void wifi_start_ap(void)
{
    ESP_LOGI(TAG, "Starting fallback AP mode...");

    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "ESP32_Fallback_AP",
            .ssid_len = strlen("ESP32_Fallback_AP"),
            .channel = 1,
            .password = "12345678",
            .max_connection = 1,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    if (strlen((char *)ap_config.ap.password) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP Mode active: SSID=%s, Password=%s",
             ap_config.ap.ssid,
             ap_config.ap.password);
}
/**
 * @brief start mdns to resolve esp hostname
*/
static void start_mdns(void)
{
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGE("mDNS", "mdns_init failed: %s", esp_err_to_name(err));
        return;
    }

    mdns_hostname_set("esp32");  
    mdns_instance_name_set("ESP32 MQTT Bluetooth Hub");

    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

    ESP_LOGI("mDNS", "mDNS responder started: http://esp32.local");
}

void wifi_init(void)
{
    char ssid[32] = {0};
    char pass[64] = {0};

    esp_err_t err = wifi_load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    if (err != ESP_OK || strlen(ssid) == 0) {
        // No stored credentials → start fallback AP immediately
        ESP_LOGW(TAG, "No Wi-Fi credentials found, starting AP mode");
        wifi_start_ap();
        httpd_manager_start(true);
        return;
    }

    s_wifi_event_group = xEventGroupCreate();
 
    esp_netif_create_default_wifi_sta();

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = "",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid)-1);
    strncpy((char*)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password)-1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "initialization finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID: %s", wifi_config.sta.ssid);
        httpd_manager_start(false);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID: %s, switching to AP mode", wifi_config.sta.password);
        wifi_start_ap();
        httpd_manager_start(true);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if ( s_current_retry < s_retry_num) {
            esp_wifi_connect();
            s_current_retry++;
            ESP_LOGI(TAG, "Retrying to connect to the AP (%d/%d)...", s_current_retry, s_retry_num);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"Connect to the AP failed");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_current_retry = 0; // reset counter if successful
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        start_mdns();
    }
}

void wifi_update_credentials(const char *ssid, const char *pass)
{
    ESP_LOGI(TAG, "Updating Wi-Fi credentials to SSID='%s'", ssid);

    // Save new credentials to NVS
    esp_err_t err = wifi_save_credentials(ssid, pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save credentials to NVS (%s)", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Restarting device to apply new credentials...");
    vTaskDelay(pdMS_TO_TICKS(1000)); // small delay for log flush
    esp_restart();
}

