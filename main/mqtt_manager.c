#include "mqtt_manager.h"
#include "common.h"

#include "mqtt_client.h"

static esp_mqtt_client_handle_t s_mqtt_client = NULL;

static void mqtt_discovery(int device_index)
{
    if (s_mqtt_client == NULL) {
        ESP_LOGE(GATTC_TAG, "MQTT client not initialized");
        return;
    }

    flood_light_device_t *device = &device_manager.devices[device_index];
    
    // Convert MAC address to string for unique ID
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X%02X%02X%02X%02X%02X", // Remove colons for cleaner ID
             device->mac_address[0], device->mac_address[1], device->mac_address[2],
             device->mac_address[3], device->mac_address[4], device->mac_address[5]);

    char discovery_topic[256];
    snprintf(discovery_topic, sizeof(discovery_topic), 
             "%s/light/esp32_floodlight_%s/config", 
             MQTT_HA_DISCOVERY_PREFIX, mac_str);

    char discovery_payload[805];
    snprintf(discovery_payload, sizeof(discovery_payload),
        "{"
        "\"name\":\"Flood Light %s\","
        "\"unique_id\":\"esp32_floodlight_%s\","
        "\"command_topic\":\"esp32/floodlight/%s/set\","
        "\"state_topic\":\"esp32/floodlight/%s/state\","
        "\"brightness_command_topic\":\"esp32/floodlight/%s/brightness/set\","
        "\"brightness_state_topic\":\"esp32/floodlight/%s/brightness/state\","
        "\"brightness_scale\":100,"
        "\"on_command_type\":\"brightness\","
        "\"on_command_type\":\"first\","
        "\"payload_off\":\"OFF\","
        "\"payload_on\":\"ON\","
        "\"state_value_template\":\"{{ value_json.state }}\"," 
        "\"brightness_value_template\":\"{{ value_json.brightness }}\"," 
        "\"optimistic\":false,"
        "\"qos\":0,"
        "\"retain\":true,"
        "\"device\":{"
            "\"identifiers\":[\"esp32_floodlight_%s\"],"
            "\"name\":\"Flood Light %s\","
            "\"manufacturer\":\"ESP32 BT Hub\","
            "\"model\":\"Flood Light\","
            "\"sw_version\":\"1.0\""
        "}"
        "}",
        mac_str, mac_str, mac_str, mac_str, mac_str, mac_str, mac_str, mac_str); // 8 arguments

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, discovery_topic, discovery_payload, 0, 1, 1);
    ESP_LOGI(GATTC_TAG, "Published discovery for device %d, msg_id=%d", device_index, msg_id);
    ESP_LOGI(GATTC_TAG, "Topic: %s", discovery_topic);
    ESP_LOGI(GATTC_TAG, "Payload: %s", discovery_payload);
}

static void mqtt_handle_command(const char* topic, int topic_len, const char* data, int data_len)
{
    char topic_str[topic_len + 1];
    char data_str[data_len + 1];

    memcpy(topic_str, topic, topic_len);
    topic_str[topic_len] = '\0';

    memcpy(data_str, data, data_len);
    data_str[data_len] = '\0';

    ESP_LOGI(GATTC_TAG, "Processing command: Topic=%s, Data=%s", topic_str, data_str);

    const char *base = "esp32/floodlight/";
    const char *p = strstr(topic_str, base);
    if (!p) return;
    p += strlen(base);

    // copy until '/' to get mac fragment
    char mac_fragment[32];
    const char *slash = strchr(p, '/');
    if (!slash) return;
    size_t mac_len = slash - p;
    if (mac_len >= sizeof(mac_fragment)) return;
    memcpy(mac_fragment, p, mac_len);
    mac_fragment[mac_len] = '\0';

    // Get the command type (what comes after MAC address)
    const char *command_type = slash + 1;

    // normalize MAC into bytes
    esp_bd_addr_t mac_addr = {0};
    bool mac_ok = false;

    // Try colon-separated parse first
    if (strchr(mac_fragment, ':')) {
        if (sscanf(mac_fragment, "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
            &mac_addr[0], &mac_addr[1], &mac_addr[2],
            &mac_addr[3], &mac_addr[4], &mac_addr[5]) == 6) {
            mac_ok = true;
        }
    } else {
        // Parse continuous hex AABBCCDDEEFF
        if (strlen(mac_fragment) == 12) {
            unsigned int b0,b1,b2,b3,b4,b5;
            if (sscanf(mac_fragment, "%2x%2x%2x%2x%2x%2x",
                &b0, &b1, &b2, &b3, &b4, &b5) == 6) {
                mac_addr[0] = (uint8_t)b0; mac_addr[1] = (uint8_t)b1; mac_addr[2] = (uint8_t)b2;
                mac_addr[3] = (uint8_t)b3; mac_addr[4] = (uint8_t)b4; mac_addr[5] = (uint8_t)b5;
                mac_ok = true;
            }
        }
    }

    if (!mac_ok) {
        ESP_LOGW(GATTC_TAG, "Unable to parse MAC from topic: %s", mac_fragment);
        return;
    }

    int device_index = find_device_by_mac(mac_addr);
    if (device_index < 0) {
        ESP_LOGW(GATTC_TAG, "No device match for MAC in topic");
        return;
    }
    ESP_LOGI(GATTC_TAG, "Found device index %d for incoming MQTT command", device_index);

    if (strcmp(command_type, "brightness/set") == 0) {
        int brightness = 0;
        if (sscanf(data_str, "%d", &brightness) == 1) {
            
            device_set_brightness(device_index, (uint8_t)brightness);
                    
        }
        return;
    }
    
    if (strcasecmp(data_str, "ON") == 0 || strcmp(data_str, "1") == 0) {
        
        ESP_LOGI(GATTC_TAG, "Turn ON device %d", device_index);
        device_set_on(device_index);
        return;
    }
    if (strcasecmp(data_str, "OFF") == 0 || strcmp(data_str, "0") == 0) {
              
        ESP_LOGI(GATTC_TAG, "Turn OFF device %d", device_index);
        device_set_off(device_index);
        return;
    }

    ESP_LOGW(GATTC_TAG, "Unhandled payload: %s", data_str);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(GATTC_TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(GATTC_TAG, "MQTT_EVENT_CONNECTED");

         // Publish discovery for all discovered devices
        for (int i = 0; i < device_manager.discovered_count; i++) {
            if (device_manager.devices[i].discovered) {
                mqtt_discovery(i);
                vTaskDelay(pdMS_TO_TICKS(100)); 
            }
        }
         // Subscribe to wildcard command topic so incoming commands reach MQTT_EVENT_DATA
        int sub_id = esp_mqtt_client_subscribe(s_mqtt_client, "esp32/floodlight/+/set", 1);
        ESP_LOGI(GATTC_TAG, "Subscribed to commands wildcard, sub_id=%d", sub_id);

        int sub_id2 = esp_mqtt_client_subscribe(s_mqtt_client, "esp32/floodlight/+/brightness/set", 1);
        ESP_LOGI(GATTC_TAG, "Subscribed to brightness wildcard, sub_id=%d", sub_id2);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(GATTC_TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(GATTC_TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(GATTC_TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(GATTC_TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(GATTC_TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        mqtt_handle_command(event->topic, event->topic_len, event->data, event->data_len);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(GATTC_TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGI(GATTC_TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGI(GATTC_TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            ESP_LOGI(GATTC_TAG, "Last captured errno : %d", event->error_handle->esp_transport_sock_errno);
        } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGI(GATTC_TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
        } else {
            ESP_LOGI(GATTC_TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
        }
        break;
    default:
        ESP_LOGI(GATTC_TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void mqtt_start(void)
{
    char client_id[32];
    uint8_t mac[6];
    
    esp_efuse_mac_get_default(mac);
    snprintf(client_id, sizeof(client_id), "esp32_bt_hub_%02x%02x%02x", mac[3], mac[4], mac[5]);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URL,
        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD,
        .credentials.client_id = client_id,
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
}

void mqtt_device_found(int index, esp_bd_addr_t mac) {
    ESP_LOGI("MQTT", "Discovered device %d, sending discovery message", index);
    mqtt_discovery(index);
}

void mqtt_device_state(int index, bool power_state, esp_bd_addr_t mac){
    if (!s_mqtt_client) {
        ESP_LOGW(GATTC_TAG, "MQTT client not ready; skipping publish");
        return;
    }
    char mac_str[18]; // "AA:BB:CC:DD:EE:FF" + null
    snprintf(mac_str, sizeof(mac_str),
             "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    char topic[64];
    snprintf(topic, sizeof(topic),
             "esp32/floodlight/%s/state", mac_str);
    const char *state_str = power_state ? "ON" : "OFF";

    char payload[64];
    snprintf(payload, sizeof(payload),
             "{\"state\":\"%s\"}", state_str);
     int msg_id = esp_mqtt_client_publish(
        s_mqtt_client,
        topic,          // topic
        payload,        // payload
        0,              // length (0 = use strlen)
        1,              // QoS 1 is typical for HA
        true            // retain message so HA sees it after restart
    );

    if (msg_id >= 0) {
        ESP_LOGI(GATTC_TAG, "Published state of device %d (%s): %s",
                 index, mac_str, payload);
    } else {
        ESP_LOGW(GATTC_TAG, "Failed to publish state for device %d", index);
    }
}