#include "httpd_manager.h"
#include "dns_server.h"

#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"

static const char *TAG = "HTTPD";

static httpd_handle_t server = NULL;
static dns_server_t *dns_server = NULL;
// Callback storage
static struct {
    wifi_credentials_cb_t wifi_credentials_cb;
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
    // Start the server
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    ESP_LOGI(TAG, "HTTP server started in %s mode", captive_portal ? "CAPTIVE PORTAL" : "NORMAL");

    if (captive_portal) {

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
            .uri = "/",
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
    }
}

void httpd_manager_set_callbacks(wifi_credentials_cb_t wifi_credentials_cb)
{
    if (wifi_credentials_cb) {
        httpd_callbacks.wifi_credentials_cb = wifi_credentials_cb;
        ESP_LOGI(TAG, "Wi-Fi credentials callback registered");
    }
}