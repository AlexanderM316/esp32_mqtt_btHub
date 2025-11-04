#include "httpd_manager.h"
#include "dns_server.h"
#include "system_metrics.h"

#include <string.h> 
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_littlefs.h"
#include "cJSON.h"
#include "nvs.h"

#include "iot_button.h" // for httpd reset login by button
#include "button_gpio.h"
#include "driver/gpio.h"

#define FLASH_BUTTON GPIO_NUM_0  // BOOT/FLASH button

#define HTTPD_NAMESPACE "httpd"

static const char *TAG = "HTTPD";

static httpd_handle_t server = NULL;
static dns_server_t *dns_server = NULL;
static button_handle_t btn = NULL;
// session info
typedef struct {
    bool is_authenticated;
    char user[16];
} session_data_t;

// Callback storage
static struct {
    wifi_credentials_cb_t wifi_credentials_cb;
    mqtt_config_cb_t mqtt_config_cb;
    ble_config_cb_t ble_config_cb;
    ble_get_config_cb_t ble_get_config_cb;
    ble_get_metrics_cb_t ble_get_metrics_cb;
    ble_get_devices_cb_t ble_get_devices_cb;
} httpd_callbacks = {0};

// simple hardcoded form for the captive portal
static const char* html_form =
"<html>"
"<body>"
"<h1>ESP32 Wi-Fi Setup</h1>"
"<form method='POST' action='/submit'>"
"SSID: <input type='text' name='ssid'><br>"
"Password: <input type='password' name='pass'><br>"
"<input type='submit' value='Connect'>"
"</form>"
"</body>"
"</html>";

static const char* get_mime_type(const char *path) {
    if (strstr(path, ".html")) return "text/html";
    if (strstr(path, ".css"))  return "text/css";
    if (strstr(path, ".js"))  return "application/javascript";
    //if (strstr(path, ".json"))  return "application/json";
    return "text/plain";
}

/**
 * @brief Captive portal handler (redirect all requests to root)
 */
static esp_err_t captive_root_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Serve root");
    httpd_resp_send(req, html_form, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
/**
 * @brief http redirection to captive root
 */ 
static esp_err_t captive_detection_handler(httpd_req_t *req, httpd_err_code_t err)
{
    
    ESP_LOGI(TAG, "Captive probe: %s", req->uri);

    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "Redirecting...", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}
/**
 * @brief submit wifi credentials
 */ 
static esp_err_t captive_submit_post(httpd_req_t *req)
{
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    char ssid[32] = {0};
    char pass[64] = {0};

    // Parse "ssid=XXX&pass=YYY"
    char *ssid_ptr = strstr(buf, "ssid=");
    char *pass_ptr = strstr(buf, "pass=");

    if (ssid_ptr && pass_ptr) {
        sscanf(ssid_ptr, "ssid=%31[^&]&pass=%63s", ssid, pass);
        ESP_LOGI(TAG, "Submitted SSID: %s, PASS: %s", ssid, pass);
        if (httpd_callbacks.wifi_credentials_cb) {
            httpd_callbacks.wifi_credentials_cb(ssid, pass);
        }
    }

    const char* resp = "<html><body><h1>Connecting...</h1></body></html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
////////////// cpative portal methods end here
/**
 * @brief helper to check session
*/
static bool check_session(httpd_req_t *req) {

    if (!server || !req) return false;

    int sockfd = httpd_req_to_sockfd(req);  
    if (sockfd < 0) return false;

    session_data_t *sess = (session_data_t *)httpd_sess_get_ctx(server, sockfd);
    if (sess && sess->is_authenticated) return true;

    char cookie_hdr[64];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_hdr, sizeof(cookie_hdr)) == ESP_OK) {
        if (strstr(cookie_hdr, "session=1")) return true;
    }
    return false;
}
/**
 * @brief load login credentails from nvs 
*/
static esp_err_t httpd_load_credentials(char* user, size_t user_len, char* pass, size_t pass_len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(HTTPD_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_get_str(nvs, "user", user, &user_len);
    if (err != ESP_OK) { nvs_close(nvs); return err; }

    err = nvs_get_str(nvs, "pass", pass, &pass_len);
    nvs_close(nvs);
    return err;
}
/**
 * @brief save login credentails to nvs 
*/
static esp_err_t httpd_save_credentials(const char* user, const char* pass)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(HTTPD_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    nvs_set_str(nvs, "user", user);
    nvs_set_str(nvs, "pass", pass);
    nvs_commit(nvs);
    nvs_close(nvs);
    return ESP_OK;
}
/**
 * @brief Resets login credentials in NVS to admin/admin
 */
static esp_err_t reset_login_credentials(void)
{
    esp_err_t err = httpd_save_credentials("admin", "admin");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset credentials: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Login credentials reset to admin/admin");
    return err;
}
/**
 * @brief set defualt credentails if they don't exist
*/
static esp_err_t check_default_credentials(void)
{
    char user[16] = {0}, pass[16] = {0};

    esp_err_t err = httpd_load_credentials(user, sizeof(user), pass, sizeof(pass));
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Keys not found → write default credentials
        esp_err_t err = reset_login_credentials();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save defualt credentials to NVS (%s)", esp_err_to_name(err));
            return err;
        }
        return ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read credentials from NVS");
        return err;
    }

    // Credentials exist, nothing to do
    return ESP_OK;
}
/**
 * @brief login page handler
 */ 
static esp_err_t login_post_handler(httpd_req_t *req) {
    
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const char *entered_user = cJSON_GetStringValue(cJSON_GetObjectItem(json, "user"));
    const char *entered_pass = cJSON_GetStringValue(cJSON_GetObjectItem(json, "pass"));
    

    if (!entered_user || !entered_pass) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing user/pass");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    // Load stored credentials from NVS
    char stored_user[16] = {0};
    char stored_pass[16] = {0};
    esp_err_t err = httpd_load_credentials(stored_user, sizeof(stored_user), stored_pass, sizeof(stored_pass));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read credentials");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

   if (strcmp(entered_user, stored_user) == 0 && strcmp(entered_pass, stored_pass) == 0) {
        // create persistent session structure
        session_data_t *sess = calloc(1, sizeof(session_data_t));
        if (!sess) {
            cJSON_Delete(json);
            return ESP_ERR_NO_MEM;
        }
        sess->is_authenticated = true;
        strncpy(sess->user, entered_user, sizeof(sess->user) - 1);

        // Persist session in httpd session store keyed by socket
        int sockfd = httpd_req_to_sockfd(req);
        if (sockfd >= 0) {
            // If there was an old ctx, free it first to avoid leak
            session_data_t *old = (session_data_t *)httpd_sess_get_ctx(server, sockfd);
            if (old) free(old);
            httpd_sess_set_ctx(server, sockfd, sess, free);
        } else {
            // fallback: keep it in req->sess_ctx for this request only
            req->sess_ctx = sess;
        }
        // Set cookie so the browser will send it on subsequent requests.
        // include Path=/ so it applies to all URIs.
        httpd_resp_set_hdr(req, "Set-Cookie", "session=1; Path=/; HttpOnly");

        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":true}");
        ESP_LOGI(TAG, "Login successful for user %s", entered_user);
    } else {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Invalid credentials");
        ESP_LOGW(TAG, "Login failed for user %s", entered_user);
    }

    cJSON_Delete(json);
    return ESP_OK;
}
/**
 * @brief get metrics data
 */ 
static esp_err_t metrics_get_handler(httpd_req_t *req)
{
    system_metrics_t *m = system_metrics_get();
    uint8_t discovered_count = 0U;
    uint8_t conn_count = 0U;

    if (httpd_callbacks.ble_get_metrics_cb) {
        httpd_callbacks.ble_get_metrics_cb( &discovered_count, &conn_count);
    }

    uint8_t indexes[discovered_count];
    const char *names[discovered_count];
    uint8_t macs[discovered_count * 6];
    bool connected[discovered_count];

    if (httpd_callbacks.ble_get_devices_cb){
        httpd_callbacks.ble_get_devices_cb( indexes, names, macs, connected);
    }

    char json[1024];
    int written = snprintf(json, sizeof(json),
        "{\"uptime_ms\":%lu,"
        "\"free_heap\":%u,"
        "\"total_heap\":%u,"
        "\"used_percent\":%.2f,"
        "\"min_free_heap\":%u,"
        "\"conn_count\":%u,"
        "\"discovered_count\":%u,"
        "\"devices\":[",
        m->uptime_ms, m->free_heap, m->total_heap, m->used_percent,
        m->min_free_heap, conn_count, discovered_count);

         for (uint8_t i = 0U; i < discovered_count; ++i) {
        int len = snprintf(json + written, sizeof(json) - (size_t)written,
            "{\"index\":%u,"
            "\"name\":\"%s\","
            "\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
            "\"connected\":%s}%s",
            indexes[i],
            names[i] != NULL ? names[i] : "",
            macs[i*6 + 0], macs[i*6 + 1], macs[i*6 + 2],
            macs[i*6 + 3], macs[i*6 + 4], macs[i*6 + 5],
            connected[i] ? "\"Connected\"" : "\"Disconnected\"",
            (i + 1U < discovered_count) ? "," : "");

        if (len < 0) {
            break; 
        }
        written += len;
        if ((size_t)written >= sizeof(json)) {
            break;
        }
    }
    if ((size_t)written < sizeof(json)) {
        (void)snprintf(json + written, sizeof(json) - (size_t)written, "]}");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
/**
 * @brief List of assets that don't require login
 * @param uri 
 * @return true/false
 */ 
static bool is_public_asset(const char *uri) {
     
    static const char *assets[] = { "/login.html", "/login.js", "/index.css" };
    for (size_t i = 0; i < sizeof(assets)/sizeof(assets[0]); i++) {
        if (strcmp(uri, assets[i]) == 0) return true;
    }
    return false;
}
/**
 * @brief Serve dynamic index.json (MQTT + BLE config)
 */
static esp_err_t index_json_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    char device_name[32] ={0};
    uint8_t tx_power = 0;
    uint8_t interval= 0;
    uint8_t duration = 0;
    uint16_t mtu = 0;
    bool by_name = false;
    bool by_uuid = false;
    uint16_t uuid = 0;
    if (httpd_callbacks.ble_get_config_cb) {
        httpd_callbacks.ble_get_config_cb( &by_name, device_name, &by_uuid, &uuid, &tx_power, &interval, &duration, &mtu);
    }

    char resp[384];
    int len = snprintf(resp, sizeof(resp),
        "{"
            "\"broker\":\" \","
            "\"prefix\":\" \","
            "\"user\":\" \","
            "\"device_name\":\"%s\","
            "\"tx_power\":%d,"
            "\"interval\":%d,"
            "\"duration\":%d,"
            "\"mtu\":%d,"
            "\"by_name\":%d,"
            "\"by_uuid\":%d,"
            "\"uuid\":%d"
        "}",
        device_name,
        (uint8_t)tx_power,
        (uint8_t)interval,
        (uint8_t)duration,
        (uint16_t)mtu,
        (uint8_t)by_name,
        (uint8_t)by_uuid,
        (uint16_t)uuid
    );

    if (len < 0 || len >= sizeof(resp)) {
        ESP_LOGE(TAG, "JSON error");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON build error");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Served /index.json");
    httpd_resp_send(req, resp, len);
    return ESP_OK;
}
/**
 * @brief normal handler
 */ 
static esp_err_t littlefs_handler(httpd_req_t *req)
{
    // If login required, redirect
    if (!check_session(req) && !is_public_asset(req->uri)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login.html");
        httpd_resp_send(req, "Redirecting to login...", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char filepath[128];
    size_t uri_len = strlen(req->uri);
    if (uri_len > 100) uri_len = 100;  // ensure it fits
    snprintf(filepath, sizeof(filepath), "/web%.*s", (int)uri_len, req->uri);

    if (req->uri[strlen(req->uri) - 1] == '/') {
        strcat(filepath, "index.html");
    }

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        ESP_LOGW(TAG, "File not found: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, get_mime_type(filepath));

    char chunk[512];
    ssize_t read_bytes;
    while ((read_bytes = read(fd, chunk, sizeof(chunk))) > 0) {
        httpd_resp_send_chunk(req, chunk, read_bytes);
    }
    close(fd);
    httpd_resp_send_chunk(req, NULL, 0); // signal end of response
    return ESP_OK;
}
/**
 * @brief set new login username/password handler
 */ 
static esp_err_t set_login_post_handler(httpd_req_t *req)
{
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty request");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const char *user = cJSON_GetStringValue(cJSON_GetObjectItem(json, "new_user"));
    const char *pass = cJSON_GetStringValue(cJSON_GetObjectItem(json, "new_pass"));

    esp_err_t err = httpd_save_credentials(user, pass);
    

    if (err != ESP_OK){
        ESP_LOGE(TAG, "Couldn't save new Login");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Successfuly saved new Login");
   

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    cJSON_Delete(json);
    return ESP_OK;
}
/**
 * @brief submit mqtt config
 */ 
static esp_err_t mqtt_submit_post(httpd_req_t *req)
{
    char buf[192];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const char *broker = cJSON_GetStringValue(cJSON_GetObjectItem(json, "broker"));
    const char *prefix = cJSON_GetStringValue(cJSON_GetObjectItem(json, "prefix"));
    const char *user   = cJSON_GetStringValue(cJSON_GetObjectItem(json, "user"));
    const char *pass   = cJSON_GetStringValue(cJSON_GetObjectItem(json, "pass"));

    ESP_LOGI(TAG, "MQTT config received: broker=%s, prefix=%s, user=%s", broker, prefix, user);

    if (broker && httpd_callbacks.mqtt_config_cb) {
        httpd_callbacks.mqtt_config_cb(broker, prefix ? prefix : "", user ? user : "", pass ? pass : "");
    }
    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}
/**
 * @brief submit ble config
 */ 
static esp_err_t ble_submit_post(httpd_req_t *req)
{
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const char *ble_name = cJSON_GetStringValue(cJSON_GetObjectItem(json, "device_name"));
    uint8_t tx_power = (uint8_t)cJSON_GetNumberValue(cJSON_GetObjectItem(json, "tx_power"));
    uint8_t interval = (uint8_t)cJSON_GetNumberValue(cJSON_GetObjectItem(json, "interval"));
    uint8_t duration = (uint8_t)cJSON_GetNumberValue(cJSON_GetObjectItem(json, "duration"));
    uint16_t mtu = (uint16_t)cJSON_GetNumberValue(cJSON_GetObjectItem(json, "mtu"));
    bool by_name = cJSON_IsTrue(cJSON_GetObjectItem(json, "by_name"));
    bool by_uuid = cJSON_IsTrue(cJSON_GetObjectItem(json, "by_uuid"));
    uint16_t uuid = (uint16_t)cJSON_GetNumberValue(cJSON_GetObjectItem(json, "uuid"));

    ESP_LOGI(TAG, "ble config received: by_name=%d, device_name=%s, by_uuid=%d, UUID=0x%04X, tx_power=%d, interval=%d, duration=%d, mtu=%d",
                by_name, ble_name, by_uuid, uuid , (int)tx_power, (int)interval, (int)duration, (int)mtu);

    httpd_callbacks.ble_config_cb( &by_name, ble_name ? ble_name : "", &by_uuid, &uuid, &tx_power, &interval, &duration, &mtu);

    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

static void long_press_cb(void *arg, void *usr_data) {
    reset_login_credentials();
}


void httpd_manager_start(bool captive_portal)
{
    if (server) {
        ESP_LOGW(TAG, "Server already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    // Start the server
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    ESP_LOGI(TAG, "HTTP server started in %s mode", captive_portal ? "CAPTIVE PORTAL" : "NORMAL");

    if (captive_portal) { // captive mode

        dns_server = dnsserver_new();
        if (dns_server) {
            ip4_addr_t ap_ip;
            ip4addr_aton("192.168.4.1", &ap_ip);
            dnsserver_set_ip(dns_server, ap_ip);
            dnsserver_start(dns_server);
            ESP_LOGI(TAG, "DNS server started");
        } else {
            ESP_LOGE(TAG, "Failed to start DNS server");
        }
        httpd_uri_t root_uri = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = captive_root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t submit_uri = {
            .uri = "/submit",
            .method = HTTP_POST,
            .handler = captive_submit_post,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &submit_uri);

        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, captive_detection_handler);
    } else { // normal mode

        check_default_credentials();

         /* configure button timings */
        button_config_t button_cfg = {
            .long_press_time = 3000  
        };

        /* configure GPIO specifics */
        button_gpio_config_t gpio_cfg = {
            .gpio_num = FLASH_BUTTON,
            .active_level = 0,         /* active low */
            .enable_power_save = false,
            .disable_pull = false    
        };

        esp_err_t err = iot_button_new_gpio_device(&button_cfg, &gpio_cfg, &btn);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create GPIO button device: %s", esp_err_to_name(err));
        } else {
            // register callback for long press start
            iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, long_press_cb, NULL);
            ESP_LOGI(TAG, "FLASH button initialized on GPIO %d; hold %d ms to reset credentials", FLASH_BUTTON, button_cfg.long_press_time);
        }

        esp_vfs_littlefs_conf_t conf = {
            .base_path = "/web",
            .partition_label = "web",
            .format_if_mount_failed = false,
        };
        esp_err_t ret = esp_vfs_littlefs_register(&conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to mount LittleFS: %s", esp_err_to_name(ret));
            return;
        }
        ESP_LOGI(TAG, "LittleFS mounted successfully");

        httpd_uri_t metrics_uri = {
            .uri       = "/metrics",
            .method    = HTTP_GET,
            .handler   = metrics_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &metrics_uri);

        httpd_uri_t index_json_uri = {
            .uri      = "/index.json",
            .method   = HTTP_GET,
            .handler  = index_json_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &index_json_uri);

        httpd_uri_t root_uri = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = littlefs_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t mqtt_submit_uri = {
            .uri = "/mqtt_submit",
            .method = HTTP_POST,
            .handler = mqtt_submit_post,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &mqtt_submit_uri);  

        httpd_uri_t ble_submit_uri = {
            .uri = "/ble_submit",
            .method = HTTP_POST,
            .handler = ble_submit_post,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &ble_submit_uri); 

        httpd_uri_t login_uri = {
            .uri = "/login",
            .method = HTTP_POST,
            .handler = login_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &login_uri);

        httpd_uri_t set_login_uri = {
            .uri = "/set_login_submit",
            .method = HTTP_POST,
            .handler = set_login_post_handler,
            .user_ctx = NULL
        };
       httpd_register_uri_handler(server, &set_login_uri);
    }
}

void httpd_manager_set_callbacks(
    wifi_credentials_cb_t wifi_credentials,
    mqtt_config_cb_t mqtt_config,
    ble_config_cb_t ble_config,
    ble_get_config_cb_t ble_get_config,
    ble_get_metrics_cb_t ble_get_metrics,
    ble_get_devices_cb_t ble_get_devices)
{
    if (wifi_credentials) httpd_callbacks.wifi_credentials_cb = wifi_credentials;
    if (mqtt_config) httpd_callbacks.mqtt_config_cb = mqtt_config;
    if (ble_config) httpd_callbacks.ble_config_cb = ble_config;
    if (ble_get_config) httpd_callbacks.ble_get_config_cb = ble_get_config;
    if (ble_get_metrics) httpd_callbacks.ble_get_metrics_cb = ble_get_metrics;
    if (ble_get_devices) httpd_callbacks.ble_get_devices_cb = ble_get_devices;
}
