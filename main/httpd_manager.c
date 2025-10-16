#include "httpd_manager.h"
#include "dns_server.h"

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_littlefs.h"
#include "cJSON.h"

static const char *TAG = "HTTPD";

static httpd_handle_t server = NULL;
static dns_server_t *dns_server = NULL;

// Callback storage
static struct {
    wifi_credentials_cb_t wifi_credentials_cb;
    mqtt_config_cb_t mqtt_config_cb;
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
/**
 * @brief normal handler
 */ 
static esp_err_t littlefs_handler(httpd_req_t *req)
{
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
    }
}

void httpd_manager_set_callbacks(
    wifi_credentials_cb_t wifi_credentials,
    mqtt_config_cb_t mqtt_config)
{
    if (wifi_credentials) httpd_callbacks.wifi_credentials_cb = wifi_credentials;
    if (mqtt_config) httpd_callbacks.mqtt_config_cb = mqtt_config;
}