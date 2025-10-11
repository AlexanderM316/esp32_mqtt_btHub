#include "device_manager.h"
#include "common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CMD_MAX_LEN 12 //  max length of w_cmd

static uint8_t w_cmd[CMD_MAX_LEN]; // default write cmd

device_manager_t device_manager = {
    .scanning = false,
    .all_devices_found = false,
    .discovered_count = 0,
    .device_found_cb = NULL,
    .all_devices_found_cb = NULL,
    .device_connected_cb = NULL,
    .device_disconnected_cb = NULL
};

// Static functions//////////////////////////////////////////////////////
static esp_bt_uuid_t notify_descr_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG,},
};
/**
 * @brief Modbus CRC function 
*/
static uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static void decode_notification(int device_index,
                                const uint8_t *data,
                                uint16_t len)
{
    if (len <= 6) return;
    flood_light_device_t *device = &device_manager.devices[device_index];
    bool new_state = (data[3] == 0x01);
    
    if (device->power_state != new_state) { // only call if changed
        device->power_state = new_state;
        if (device_manager.device_power_state_cb) {
            device_manager.device_power_state_cb(device_index, device->power_state, device->mac_address);
        }
    }
}
/**
 * @brief build the w_cmd
 * @param opcode the command type (0x11 = on/off, 0x13 = brightness 0x17 rbg)
 * @param *payload data
 * @param paylaod_len size of the payload data only not the whole w_cmd
 * @return Total length 
*/
static size_t build_cmd(uint8_t opcode, const uint8_t *payload, size_t payload_len)
{
    w_cmd[0] = 0xAA;        // header
    w_cmd[1] = opcode;      // 0x11 = on/off, 0x13 = brightness
    w_cmd[2] = 3 + payload_len;

    for (size_t i = 0; i < payload_len; i++) {
        w_cmd[3 + i] = payload[i];
    }
    
    uint16_t crc = crc16_modbus(w_cmd, 3 + payload_len);  // append to end
    w_cmd[3 + payload_len] = crc & 0xFF;                    // low byte first
    w_cmd[4 + payload_len] = (crc >> 8) & 0xFF;             // high byte

    ESP_LOGI(GATTC_TAG, "Built command buffer:");
    ESP_LOG_BUFFER_HEX(GATTC_TAG, w_cmd, sizeof(w_cmd));
    size_t pkt_len = 3 + payload_len + 2; // header + payload + crc
    if (pkt_len > CMD_MAX_LEN) return 0;
    return pkt_len;
}

static void start_scanning(void)
{
    if (device_manager.all_devices_found) {
        ESP_LOGI(GATTC_TAG, "All devices already found");
        return;
    }
    
    device_manager.scanning = true;
    ESP_LOGI(GATTC_TAG, "Starting discovery for %d devices ...", MAX_DEVICES);
    esp_ble_gap_start_scanning(30);
}

static void stop_scanning(void)
{
    device_manager.scanning = false;
    esp_ble_gap_stop_scanning();
    ESP_LOGI(GATTC_TAG, "Scanning stopped");
}

static bool control_device(int device_index, uint8_t *data, uint16_t length)
{ 

    if (device_index < 0 || device_index >= MAX_DEVICES) {
        ESP_LOGE(GATTC_TAG, "Invalid device index: %d", device_index);
        return false;
    }
    
    flood_light_device_t *device = &device_manager.devices[device_index];
    
    
    if (!device->connected) {
        ESP_LOGE(GATTC_TAG, "Device %d is not connected... connecting", device_index);
        if(!connect_to_device(device_index)){
            ESP_LOGE(GATTC_TAG, "Failed to connect to device, abort");
            return false;
        }
        
    }
    
    if (device->char_handle == 0) {
        ESP_LOGE(GATTC_TAG, "No characteristic handle for device %d", device_index);
        return false;
    }

    if (device->write_char_handle  == 0) {
        ESP_LOGE(GATTC_TAG, "No suitable characteristic handle for device %d", device_index);
        return false;
    }
    
    esp_gatt_status_t ret = esp_ble_gattc_write_char(
        device->gattc_if,
        device->conn_id,
        device->write_char_handle,
        length,
        data,
        ESP_GATT_WRITE_TYPE_NO_RSP,
        ESP_GATT_AUTH_REQ_NONE);
        
    if (ret == ESP_GATT_OK) {
        ESP_LOGI(GATTC_TAG, "Successfully controlled Flood Light %d", device_index);
        return true;
    } else {
        ESP_LOGE(GATTC_TAG, "Failed to control device %d: %d", device_index, ret);
        return false;
    }
}
// Unified device event handler
static void gattc_device_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param, int device_index)
{
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;
    flood_light_device_t *device = &device_manager.devices[device_index];

    switch (event) {
    case ESP_GATTC_REG_EVT:
        ESP_LOGI(GATTC_TAG, "Device %d registered with gattc_if %d", device_index, gattc_if);
        device->gattc_if = gattc_if;
        break;
        
    case ESP_GATTC_OPEN_EVT:
        if (p_data->open.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "Device %d: connect failed, status %d", device_index, p_data->open.status);
            device->connected = false;
            break;
        }
        
        device->conn_id = p_data->open.conn_id;
        device->connected = true;
        
        ESP_LOGI(GATTC_TAG, "Device %d: Successfully connected", device_index);
        
        // Notify callback
        if (device_manager.device_connected_cb) {
            device_manager.device_connected_cb(device_index);
        }
        
        esp_err_t mtu_ret = esp_ble_gattc_send_mtu_req(gattc_if, p_data->open.conn_id);
        if (mtu_ret){
            ESP_LOGE(GATTC_TAG, "Device %d: MTU error = %x", device_index, mtu_ret);
        }
        break;
        
    case ESP_GATTC_CFG_MTU_EVT:
        if (param->cfg_mtu.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "Device %d: MTU config failed", device_index);
        } else {
            ESP_LOGI(GATTC_TAG, "Device %d: MTU %d", device_index, param->cfg_mtu.mtu);
            esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, NULL); //, &remote_filter_service_uuid);
        }
        break;
        
    case ESP_GATTC_SEARCH_RES_EVT: {
        if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 
            && p_data->search_res.srvc_id.uuid.uuid.uuid16 == REMOTE_SERVICE_UUID
            ) 
        {
            ESP_LOGI(GATTC_TAG, "Device %d: Found service Service UUID: 0x%0X", device_index, p_data->search_res.srvc_id.uuid.uuid.uuid16);
            device->service_start_handle = p_data->search_res.start_handle;
            device->service_end_handle = p_data->search_res.end_handle;
        }
        break;
    }
    
    case ESP_GATTC_SEARCH_CMPL_EVT:
        if (p_data->search_cmpl.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "Device %d: service search failed = %x", device_index, p_data->search_cmpl.status);
            break;
        }

        ESP_LOGI(GATTC_TAG, "Device %d: Service discovery complete", device_index);

        // Get characteristics count
        uint16_t count = 0;
        esp_gatt_status_t status = esp_ble_gattc_get_attr_count(
            gattc_if,
            device->conn_id,
            ESP_GATT_DB_CHARACTERISTIC,
            device->service_start_handle,
            device->service_end_handle,
            INVALID_HANDLE,
            &count);

        if (status != ESP_GATT_OK || count == 0) {
            ESP_LOGE(GATTC_TAG, "Device %d: No characteristics found", device_index);
            break;
        }

        esp_gattc_char_elem_t *char_elem_result = malloc(sizeof(esp_gattc_char_elem_t) * count);
        if (!char_elem_result) {
            ESP_LOGE(GATTC_TAG, "Device %d: No memory for characteristics", device_index);
            break;
        }

        // LOOK FOR NOTIFY CHARACTERISTIC (0xFF01)
        esp_bt_uuid_t notify_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid = {.uuid16 = REMOTE_NOTIFY_CHAR_UUID,},
        };

        uint16_t notify_count = count;
        esp_gatt_status_t st1 = esp_ble_gattc_get_char_by_uuid(
            gattc_if,
            device->conn_id,
            device->service_start_handle,
            device->service_end_handle,
            notify_uuid,
            char_elem_result,
            &notify_count);

        if (st1 == ESP_GATT_OK && notify_count > 0 && 
            (char_elem_result[0].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY)) {
            device->char_handle = char_elem_result[0].char_handle;
            esp_ble_gattc_register_for_notify(gattc_if, device->mac_address, char_elem_result[0].char_handle);
            ESP_LOGI(GATTC_TAG, "Device %d: Registered for notifications (handle 0x%08x)", device_index, device->char_handle);
        }

        esp_bt_uuid_t write_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid = {.uuid16 = REMOTE_WRITE_CHAR_UUID,},
        };

        uint16_t write_count = count;
        esp_gatt_status_t st2 = esp_ble_gattc_get_char_by_uuid(
            gattc_if,
            device->conn_id,
            device->service_start_handle,
            device->service_end_handle,
            write_uuid,
            char_elem_result,
            &write_count);

        if (st2 == ESP_GATT_OK && write_count > 0 &&
            (char_elem_result[0].properties & (ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR))) {
            device->write_char_handle = char_elem_result[0].char_handle;
            ESP_LOGI(GATTC_TAG, "Device %d: Found write characteristic (handle 0x%08x)", device_index, device->write_char_handle);
        } else {
            ESP_LOGW(GATTC_TAG, "Device %d: write characteristic not found (UUID 0x%04x)", device_index, REMOTE_WRITE_CHAR_UUID);
        }

        free(char_elem_result);
        break;

        
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        if (p_data->reg_for_notify.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "Device %d: notify registration failed = %x", device_index, p_data->reg_for_notify.status);
            break;
        }
        
        uint16_t count = 0;
        uint16_t notify_en = 1;
        esp_gatt_status_t ret_status = esp_ble_gattc_get_attr_count(
            gattc_if,
            device->conn_id,
            ESP_GATT_DB_DESCRIPTOR,
            device->service_start_handle,
            device->service_end_handle,
            device->char_handle,
            &count);
            
        if (ret_status != ESP_GATT_OK || count == 0){
            ESP_LOGE(GATTC_TAG, "Device %d: no descriptors found", device_index);
            break;
        }
        
        esp_gattc_descr_elem_t *descr_elem_result = malloc(sizeof(esp_gattc_descr_elem_t) * count);
        if (!descr_elem_result){
            ESP_LOGE(GATTC_TAG, "Device %d: no memory for descriptors", device_index);
            break;
        }
        
        ret_status = esp_ble_gattc_get_descr_by_char_handle(
            gattc_if,
            device->conn_id,
            p_data->reg_for_notify.handle,
            notify_descr_uuid,
            descr_elem_result,
            &count);
            
        if (ret_status == ESP_GATT_OK && count > 0 && 
            descr_elem_result[0].uuid.len == ESP_UUID_LEN_16 && 
            descr_elem_result[0].uuid.uuid.uuid16 == ESP_GATT_UUID_CHAR_CLIENT_CONFIG) {
            
            esp_ble_gattc_write_char_descr(
                gattc_if,
                device->conn_id,
                descr_elem_result[0].handle,
                sizeof(notify_en),
                (uint8_t *)&notify_en,
                ESP_GATT_WRITE_TYPE_RSP,
                ESP_GATT_AUTH_REQ_NONE);
                
            ESP_LOGI(GATTC_TAG, "Device %d: Notifications enabled", device_index);
        }
        
        free(descr_elem_result);
        break;
    }

    case ESP_GATTC_NOTIFY_EVT:
        ESP_LOGI(GATTC_TAG, "Device %d: Received notification", device_index);
        ESP_LOG_BUFFER_HEX(GATTC_TAG, p_data->notify.value, p_data->notify.value_len);
        decode_notification(device_index,p_data->notify.value, p_data->notify.value_len);
        break;
        
    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGI(GATTC_TAG, "Device %d: Disconnected", device_index);
        device->connected = false;
        device->conn_id = 0;
        device->char_handle = 0;
        
        // Notify callback
        if (device_manager.device_disconnected_cb) {
            device_manager.device_disconnected_cb(device_index);
        }
        break;
        
    default:
        break;
    }
}

void device_manager_init(void)
{
    // Initialize all devices
    for (int i = 0; i < MAX_DEVICES; i++) {
        device_manager.devices[i].discovered = false;
        device_manager.devices[i].connected = false;
        device_manager.devices[i].power_state = false;
        device_manager.devices[i].gattc_if = ESP_GATT_IF_NONE;
        device_manager.devices[i].app_id = i;
        device_manager.devices[i].char_handle = 0;
        device_manager.devices[i].write_char_handle = 0;
        memset(device_manager.devices[i].mac_address, 0, ESP_BD_ADDR_LEN);
        memset(device_manager.devices[i].name, 0, sizeof(device_manager.devices[i].name));
    }
    
    device_manager.discovered_count = 0;
    device_manager.all_devices_found = false;
    
    // Set default callbacks
    device_manager.device_found_cb = NULL;
    device_manager.all_devices_found_cb = NULL;
    device_manager.device_connected_cb = NULL;
    device_manager.device_disconnected_cb = NULL;
    
    ESP_LOGI(GATTC_TAG, "Device Manager initialized for %d devices", MAX_DEVICES);
}

void device_manager_set_callbacks(
    device_found_cb_t device_found, 
    all_devices_found_cb_t all_found,
    device_connected_cb_t device_connected,
    device_disconnected_cb_t device_disconnected,
    device_power_state_cb_t device_power_state)
{
    if (device_found) device_manager.device_found_cb = device_found;
    if (all_found) device_manager.all_devices_found_cb = all_found;
    if (device_connected) device_manager.device_connected_cb = device_connected;
    if (device_disconnected) device_manager.device_disconnected_cb = device_disconnected;
    if (device_power_state) device_manager.device_power_state_cb = device_power_state;
}

void start_device_discovery(void)
{
    if (device_manager.scanning) {
        ESP_LOGI(GATTC_TAG, "Discovery already in progress");
        return;
    }
    
    device_manager.all_devices_found = false;
    start_scanning();
}

int find_device_by_mac(esp_bd_addr_t mac_addr)
{
    for (int i = 0; i < device_manager.discovered_count; i++) {
        if (memcmp(device_manager.devices[i].mac_address, mac_addr, ESP_BD_ADDR_LEN) == 0) {
            return i;
        }
    }
    return -1; // Not found
}

bool connect_to_device(int device_index)
{
    if (device_index < 0 || device_index >= device_manager.discovered_count) {
        ESP_LOGE(GATTC_TAG, "Invalid device index: %d", device_index);
        return false;
    }
    
    flood_light_device_t *device = &device_manager.devices[device_index];
    
    if (device->connected) {
        ESP_LOGI(GATTC_TAG, "Device %d already connected", device_index);
        return true;
    }
    
    if (device->gattc_if == ESP_GATT_IF_NONE) {
        ESP_LOGE(GATTC_TAG, "Device %d not registered yet", device_index);
        return false;
    }
    
    ESP_LOGI(GATTC_TAG, "Connecting to Flood Light %d", device_index);
    
    esp_err_t ret = esp_ble_gattc_open(device->gattc_if, device->mac_address, BLE_ADDR_TYPE_PUBLIC, true);
    if (ret != ESP_OK) {
        ESP_LOGE(GATTC_TAG, "Failed to initiate connection: %d", ret);
        return false;
    }
    
    return true;
}

bool disconnect_from_device(int device_index)
{
    if (device_index < 0 || device_index >= MAX_DEVICES) {
        ESP_LOGE(GATTC_TAG, "Invalid device index: %d", device_index);
        return false;
    }
    
    flood_light_device_t *device = &device_manager.devices[device_index];
    
    if (!device->connected) {
        ESP_LOGE(GATTC_TAG, "Device %d is not connected", device_index);
        return false;
    }
    
    esp_err_t ret = esp_ble_gattc_close(device->gattc_if, device->conn_id);
    if (ret != ESP_OK) {
        ESP_LOGE(GATTC_TAG, "Failed to disconnect device %d: %d", device_index, ret);
        return false;
    }
    
    return true;
}

bool device_set_on(int device_index)
{
    uint8_t payload = 0x01;
    size_t cmd_len = build_cmd(0x11, &payload, 1);
    if (!cmd_len) return false;
    return control_device(device_index, w_cmd, cmd_len);
}

bool device_set_off(int device_index)
{
    uint8_t payload = 0x00;
    size_t cmd_len = build_cmd(0x11, &payload, 1);
    if (!cmd_len) return false;
    return control_device(device_index, w_cmd, cmd_len);
}

bool device_set_brightness(int device_index, uint8_t brightness)
{   
    size_t cmd_len = build_cmd(0x13, &brightness, 1);
    if (!cmd_len) return false;
    return control_device(device_index, w_cmd, cmd_len);
}

bool device_set_color(int device_index, uint8_t r, uint8_t g, uint8_t b)
{  
    uint8_t payload[7] = {r, g, b, r, g, b, 0x64}; 
    size_t cmd_len = build_cmd(0x17, payload, 7);
    if (!cmd_len) return false;
    return control_device(device_index, w_cmd, cmd_len);
}

void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    uint8_t *adv_name = NULL;
    uint8_t adv_name_len = 0;

    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        start_scanning();
        break;
        
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (!(param->scan_start_cmpl.status == ESP_BT_STATUS_SUCCESS)) {
             ESP_LOGE(GATTC_TAG, "Scan start failed");
        }
        break;
        
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
        
        switch (scan_result->scan_rst.search_evt) {
        case ESP_GAP_SEARCH_INQ_RES_EVT:
            if (device_manager.all_devices_found) {
                break;
            }
            
            adv_name = esp_ble_resolve_adv_data_by_type(
                scan_result->scan_rst.ble_adv,
                scan_result->scan_rst.adv_data_len + scan_result->scan_rst.scan_rsp_len,
                ESP_BLE_AD_TYPE_NAME_CMPL,
                &adv_name_len);
                
            if (adv_name != NULL) {
                if (strlen(REMOTE_DEVICE_NAME) == adv_name_len && 
                    strncmp((char *)adv_name, REMOTE_DEVICE_NAME, adv_name_len) == 0) {

                    // Check if we already have this device
                    bool exists = false;
                    for (int i = 0; i < device_manager.discovered_count; i++) {
                        if (memcmp(device_manager.devices[i].mac_address, 
                                 scan_result->scan_rst.bda, ESP_BD_ADDR_LEN) == 0) {
                            exists = true;
                            break;
                        }
                    }

                    if (!exists && device_manager.discovered_count < MAX_DEVICES) {
                        int new_index = device_manager.discovered_count;
                        flood_light_device_t *device = &device_manager.devices[new_index];
                        
                        memcpy(device->mac_address, scan_result->scan_rst.bda, ESP_BD_ADDR_LEN);
                        strncpy(device->name, REMOTE_DEVICE_NAME, sizeof(device->name) - 1);
                        device->discovered = true;
                        
                        ESP_LOGI(GATTC_TAG, "Discovered Flood Light #%d", new_index);
                        ESP_LOG_BUFFER_HEX(GATTC_TAG, scan_result->scan_rst.bda, 6);
                        
                        device_manager.discovered_count++;

                        // Notify callback
                        if (device_manager.device_found_cb) {
                            device_manager.device_found_cb(new_index, scan_result->scan_rst.bda);
                        }

                        // Check if we found all devices
                        if (device_manager.discovered_count >= MAX_DEVICES) {
                            device_manager.all_devices_found = true;
                            stop_scanning();
                            ESP_LOGI(GATTC_TAG, "All %d Flood Lights found!", MAX_DEVICES);
                            
                            if (device_manager.all_devices_found_cb) {
                                device_manager.all_devices_found_cb();
                            }
                        }
                    }
                }
            }
            break;
            
        case ESP_GAP_SEARCH_INQ_CMPL_EVT:
            if (!device_manager.all_devices_found) {
                ESP_LOGI(GATTC_TAG, "Scan completed, found %d/%d devices. Restarting scan in 3s", 
                        device_manager.discovered_count, MAX_DEVICES);
                vTaskDelay(pdMS_TO_TICKS(3000));
                start_scanning();
            } else {
                ESP_LOGI(GATTC_TAG, "Scan completed - all devices found");
            }
            break;
            
        default:
            break;
        }
        break;
    }
    
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "Scan stop failed");
        } else {
            ESP_LOGI(GATTC_TAG, "Scan stopped successfully");
            device_manager.scanning = false;
        }
        break;
        
    default:
        break;
    }
}

void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    int device_index = -1;

    if (event == ESP_GATTC_REG_EVT) {
        // Store the gattc_if for this device (app_id is the device index)
        device_index = param->reg.app_id;
        if (device_index < MAX_DEVICES) {
            device_manager.devices[device_index].gattc_if = gattc_if;
            ESP_LOGI(GATTC_TAG, "Device %d assigned gattc_if %d", device_index, gattc_if);
        }
    } else {
        // For other events, find the device by gattc_if
        for (int i = 0; i < MAX_DEVICES; i++) {
            if (device_manager.devices[i].gattc_if == gattc_if) {
                device_index = i;
                break;
            }
        }
    }

    if (device_index >= 0 && device_index < MAX_DEVICES) {
        gattc_device_event_handler(event, gattc_if, param, device_index);
    } else {
        ESP_LOGW(GATTC_TAG, "Event %d for unknown gattc_if: %d", event, gattc_if);
    }
}
