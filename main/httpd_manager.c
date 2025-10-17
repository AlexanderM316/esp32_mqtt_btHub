#include "httpd_manager.h"
#include "dns_server.h"

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
    if (strstr(path, ".json"))  return "application/json";
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
    session_data_t *sess = (session_data_t *)req->sess_ctx;
    return sess && sess->is_authenticated;
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
        // Allocate session context if not already
        if (!req->sess_ctx) {
            req->sess_ctx = malloc(sizeof(session_data_t));
            if (!req->sess_ctx) return ESP_ERR_NO_MEM;
        }
        session_data_t *sess = (session_data_t *)req->sess_ctx;
        sess->is_authenticated = true;
        strncpy(sess->user, entered_user, sizeof(sess->user) - 1);

        // Set HttpOnly cookie
        httpd_resp_set_hdr(req, "Set-Cookie", "session=1; HttpOnly");

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
    char buf[64];
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

    ESP_LOGI(TAG, "ble config received: device_name=%s", ble_name);

    httpd_callbacks.ble_config_cb( ble_name ? ble_name : "");

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
 
    }
}

void httpd_manager_set_callbacks(
    wifi_credentials_cb_t wifi_credentials,
    mqtt_config_cb_t mqtt_config,
    ble_config_cb_t ble_config)
{
    if (wifi_credentials) httpd_callbacks.wifi_credentials_cb = wifi_credentials;
    if (mqtt_config) httpd_callbacks.mqtt_config_cb = mqtt_config;
    if (ble_config) httpd_callbacks.ble_config_cb = ble_config;
}