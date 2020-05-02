#ifndef __APP_BLE__
#define __APP_BLE__

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_bt.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_defs.h"
#include "esp_gattc_api.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define APP_BLE_APP_ID (0)
#define INVALID_HANDLE (0)

#define ERR_CHECK(err, text)                                   \
    {                                                          \
        esp_err_t res;                                         \
        if ((res = err) != ESP_OK)                             \
        {                                                      \
            ESP_LOGE(TAG, "ERR_CHECK fail %s, %d", text, res); \
        }                                                      \
    }

#define LOG_UUID(tag, uid) ESP_LOGI(tag, "UUID: %02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",                                        \
                               uid.uuid.uuid128[0], uid.uuid.uuid128[1], uid.uuid.uuid128[2], uid.uuid.uuid128[3], uid.uuid.uuid128[4], uid.uuid.uuid128[5],   \
                               uid.uuid.uuid128[6], uid.uuid.uuid128[7], uid.uuid.uuid128[8], uid.uuid.uuid128[9], uid.uuid.uuid128[10], uid.uuid.uuid128[11], \
                               uid.uuid.uuid128[12], uid.uuid.uuid128[13], uid.uuid.uuid128[14], uid.uuid.uuid128[15]);

typedef void (*discovery_handler)(const char *name, int len, esp_bd_addr_t adr, int adr_type);

#define BLE_NOTIFICATION 0x0001
#define BLE_INDICATION 0x0002

const char *ble_key_type_to_str(esp_ble_key_type_t key_type);
char *ble_auth_req_to_str(esp_ble_auth_req_t auth_req);

void ble_init();

void ble_scan_start(discovery_handler handler);
void ble_scan_stop();

void ble_connect(uint8_t *address, int type);
void ble_disconnect();

int ble_get_chars(esp_gatt_if_t gatt_if, uint16_t service_start, uint16_t service_end, uint8_t* searchUUIDs, int numUUIDs, uint16_t* resultHandles);

bool ble_write_char(uint16_t handle, uint8_t *data, int dataLength);
bool ble_write_char_secure(uint16_t handle, uint8_t *data, int dataLength);

void ble_enable_indication(uint16_t service_start, uint16_t service_end, uint16_t handle);
void ble_enable_notification(uint16_t service_start, uint16_t service_end, uint16_t handle, bool safe);

#endif