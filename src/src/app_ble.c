#include "app_ble.h"
#include "canon_ble.h"

#define TAG "BLE"

/*
Connection process:
    1. ESP_GATTC_OPEN_EVT -> esp_ble_gattc_send_mtu_req (set remote MTU)
    2. ESP_GATTC_CFG_MTU_EVT -> esp_ble_gattc_search_service (search remove services)
    3. [multiple] ESP_GATTC_SEARCH_RES_EVT -> Service discovered
    4. ESP_GATTC_SEARCH_CMPL_EVT -> Discovery completed -> Get required characteristics
    5. Do any kind of request with ESP_GATT_AUTH_REQ_SIGNED_MITM -> Initiates BONDING
    6. ESP_GAP_BLE_AUTH_CMPL_EVT -> Bonding result -> If SUCCESS ready to communicate
*/

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x50,
    .scan_window = 0x30,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE};

static discovery_handler scan_handler;

static esp_gatt_if_t gatt_if;

static uint16_t gatt_handle = ESP_GATT_IF_NONE;
static uint16_t conn_id;
static esp_bd_addr_t remote_bda;

static void ble_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    esp_err_t err;

    switch (event)
    {
    case ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT:
        if (param->local_privacy_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(TAG, "config local privacy failed, error code =%x", param->local_privacy_cmpl.status);
            break;
        }
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(TAG, "scan start failed, error status = %x", param->scan_start_cmpl.status);
            break;
        }
        ESP_LOGI(TAG, "Scan start success");
        break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
    {
        if ((err = param->scan_stop_cmpl.status) != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(TAG, "Scan stop failed: %s", esp_err_to_name(err));
        }
        else
        {
            ESP_LOGI(TAG, "Stop scan successfully");
        }
        break;
    }
    case ESP_GAP_BLE_OOB_REQ_EVT:
    {
        ESP_LOGI(TAG, "ESP_GAP_BLE_OOB_REQ_EVT");
        uint8_t tk[16] = {1}; //If you paired with OOB, both devices need to use the same tk
        esp_ble_oob_req_reply(param->ble_security.ble_req.bd_addr, tk, sizeof(tk));
        break;
    }
    case ESP_GAP_BLE_SEC_REQ_EVT:
        /* send the positive(true) security response to the peer device to accept the security request.
        If not accept the security request, should send the security response with negative(false) accept value*/
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_NC_REQ_EVT:
        /* The app will receive this evt when the IO has DisplayYesNO capability and the peer device IO also has DisplayYesNo capability.
        show the passkey number to the user to confirm it with the number displayed by peer device. */
        esp_ble_confirm_reply(param->ble_security.ble_req.bd_addr, true);
        ESP_LOGI(TAG, "ESP_GAP_BLE_NC_REQ_EVT, the passkey Notify number:%d", param->ble_security.key_notif.passkey);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
    {
        if (!param->ble_security.auth_cmpl.success)
        {
            ESP_LOGI(TAG, "Bond FAIL reason = 0x%x", param->ble_security.auth_cmpl.fail_reason);
        }
        else
        {
            ESP_LOGI(TAG, "Bond DONE");
        }

        canon_bond_result(param->ble_security.auth_cmpl.success);

        break;
    }
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
    {
        esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;

        uint8_t *adv_name = NULL;
        uint8_t adv_name_len = 0;
        adv_name = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv,
                                            ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);

        if (adv_name != NULL)
        {
            adv_name_len = strlen((char *)adv_name); // adv_name_len != real string length
            adv_name[adv_name_len] = 0;

            scan_handler((char *)adv_name, adv_name_len, scan_result->scan_rst.bda, scan_result->scan_rst.ble_addr_type);
        }
        break;
    }
    default:
        break;
    }
}

static void ble_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;

    switch (event)
    {
    case ESP_GATTC_REG_EVT:
        esp_ble_gap_config_local_privacy(true);
        break;
    case ESP_GATTC_OPEN_EVT:
    {
        if (param->open.status != ESP_GATT_OK)
        {
            ESP_LOGE(TAG, "Connection failed, %x", p_data->open.status);
            break;
        }
        ESP_LOGI(TAG, "Connection success");

        gatt_if = gattc_if;

        conn_id = p_data->open.conn_id;
        memcpy(remote_bda, p_data->open.remote_bda, sizeof(esp_bd_addr_t));

        ERR_CHECK(esp_ble_gattc_send_mtu_req(gattc_if, p_data->open.conn_id), "Send MTU");
        break;
    }
    case ESP_GATTC_CFG_MTU_EVT:
    {
        if (param->cfg_mtu.status != ESP_GATT_OK)
        {
            ESP_LOGE(TAG, "MTU config failed, %x", param->cfg_mtu.status);
        }
        ESP_LOGI(TAG, "ESP_GATTC_CFG_MTU_EVT, Status %d, MTU %d, conn_id %d", param->cfg_mtu.status, param->cfg_mtu.mtu, param->cfg_mtu.conn_id);
        esp_ble_gattc_search_service(gattc_if, param->cfg_mtu.conn_id, NULL); // NULL filter, find all services
        break;
    }
    case ESP_GATTC_SEARCH_RES_EVT:
    {
        ESP_LOGI(TAG, "ESP_GATTC_SEARCH_RES_EVT: conn_id = %x is primary service %d", p_data->search_res.conn_id, p_data->search_res.is_primary);
        ESP_LOGI(TAG, "start handle %d end handle %d current handle value %d", p_data->search_res.start_handle, p_data->search_res.end_handle, p_data->search_res.srvc_id.inst_id);

        // Find services and save their handles
        esp_bt_uuid_t serviceUUID = p_data->search_res.srvc_id.uuid;

        canon_service_discovery(serviceUUID, p_data->search_res.start_handle, p_data->search_res.end_handle);
        break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT:
    {
        if (p_data->search_cmpl.status != ESP_GATT_OK)
        {
            ESP_LOGE(TAG, "Search service failed, error status = %x", p_data->search_cmpl.status);
            break;
        }
        if (p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_REMOTE_DEVICE)
        {
            ESP_LOGI(TAG, "Got service information from remote device");
        }
        else if (p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_NVS_FLASH)
        {
            ESP_LOGI(TAG, "Got service information from flash");
        }
        else
        {
            ESP_LOGI(TAG, "Unknown service source");
        }

        canon_discovery_complete(gattc_if);
        break;
    }
    case ESP_GATTC_WRITE_CHAR_EVT:
    {
        if (p_data->write.status != ESP_GATT_OK)
        {
            ESP_LOGE(TAG, "Write char failed, error status = %x", p_data->write.status);
        }

        canon_char_write_result(p_data->write.status == ESP_GATT_OK);
        break;
    }
    case ESP_GATTC_WRITE_DESCR_EVT:
    {
        if (p_data->write.status != ESP_GATT_OK)
        {
            ESP_LOGE(TAG, "write descr failed, error status = %x", p_data->write.status);
        }
        else
        {
            ESP_LOGI(TAG, "write descr ok");
        }

        canon_chardesc_write_result(p_data->write.status == ESP_GATT_OK);
        break;
    }
    case ESP_GATTC_NOTIFY_EVT:
    {
        ESP_LOGI(TAG, "ESP_GATTC_NOTIFY_EVT, receive notify value:");
        esp_log_buffer_hex(TAG, p_data->notify.value, p_data->notify.value_len);

        canon_char_notify(p_data->notify.handle, p_data->notify.value, p_data->notify.value_len);
        break;
    }
    case ESP_GATTC_DISCONNECT_EVT:
    {
        ESP_LOGI(TAG, "ESP_GATTC_DISCONNECT_EVT");
        canon_disconnect();
        break;
    }
    default:
        break;
    }
}

static void ble_main_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    if (event == ESP_GATTC_REG_EVT)
    {
        if (param->reg.status == ESP_GATT_OK)
        {
            gatt_handle = gattc_if;
        }
        else
        {
            ESP_LOGI(TAG, "Reg app failed, app_id %04x, status %d",
                     param->reg.app_id,
                     param->reg.status);
            return;
        }
    }

    if (gattc_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
        gattc_if == gatt_handle)
    {
        ble_gattc_cb(event, gattc_if, param);
    }
}

int ble_get_chars(esp_gatt_if_t gatt_if, uint16_t service_start, uint16_t service_end, uint8_t *searchUUIDs, int numUUIDs, uint16_t *resultHandles)
{
    int found = 0;

    uint16_t count = 0;
    uint16_t offset = 0;

    // Get the number of attributes
    esp_gatt_status_t ret = esp_ble_gattc_get_attr_count(gatt_if, conn_id, ESP_GATT_DB_CHARACTERISTIC, service_start, service_end, INVALID_HANDLE, &count);
    if (ret != ESP_GATT_OK)
    {
        ESP_LOGE(TAG, "esp_ble_gattc_get_attr_count error, %d", __LINE__);
    }
    else
    {
        if (count > 0)
        {
            // Allocate array
            esp_gattc_char_elem_t *char_elem_result = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
            if (!char_elem_result)
            {
                ESP_LOGE(TAG, "ble_get_chars no mem");
            }
            else
            {
                // Get characteristics
                ret = esp_ble_gattc_get_all_char(gatt_if, conn_id, service_start, service_end, char_elem_result, &count, offset);
                if (ret != ESP_GATT_OK)
                {
                    ESP_LOGE(TAG, "esp_ble_gattc_get_all_char error, %d", __LINE__);
                }
                if (count > 0)
                {
                    for (int i = 0; i < count; ++i)
                    {
                        // Compare UUIDs
                        esp_bt_uuid_t chr_uuid = char_elem_result[i].uuid;
                        if (chr_uuid.len == ESP_UUID_LEN_128)
                        {
                            //LOG_UUID(TAG, chr_uuid);

                            for (int findIndex = 0; findIndex < numUUIDs; findIndex++)
                            {
                                uint8_t *uuidPtr = &searchUUIDs[ESP_UUID_LEN_128 * findIndex];
                                if (memcmp(chr_uuid.uuid.uuid128, uuidPtr, ESP_UUID_LEN_128) == 0)
                                {
                                    resultHandles[findIndex] = char_elem_result[i].char_handle;

                                    found++;
                                }
                            }
                        }
                        else
                        {
                            ESP_LOGW(TAG, "Only 128bit characteristics UUIDs supported!");
                        }
                    }
                }
            }

            // Free the allocated array
            free(char_elem_result);
        }
    }

    return found;
}

void ble_init()
{
    // Init and enable the Bluetooth controller in BLE mode
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ERR_CHECK(esp_bt_controller_init(&bt_cfg), "ctrl_init");
    ERR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE), "ctrl_enable");

    // Init and enable BlueDroid
    ERR_CHECK(esp_bluedroid_init(), "bd_init");
    ERR_CHECK(esp_bluedroid_enable(), "bd_enable");

    // Register callbacks
    ERR_CHECK(esp_ble_gap_register_callback(ble_gap_cb), "gap_cb");
    ERR_CHECK(esp_ble_gattc_register_callback(ble_main_gattc_cb), "gatt_cb");

    // Register GATTC app
    ERR_CHECK(esp_ble_gattc_app_register(APP_BLE_APP_ID), "gatt_app_reg");

    // Set the MTU size
    ERR_CHECK(esp_ble_gatt_set_local_mtu(200), "set_mtu");

    // Setup security for PAIR and BONDING
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_MITM_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t oob_support = ESP_BLE_OOB_DISABLE;
    ERR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t)), "sec_mode");
    ERR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t)), "sec_iocap");
    ERR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t)), "sec_keysize");
    ERR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_OOB_SUPPORT, &oob_support, sizeof(uint8_t)), "sec_oob");
    ERR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t)), "sec_initkey");
    ERR_CHECK(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t)), "yec_rspkey");

    // Set the scan params
    esp_ble_gap_set_scan_params(&ble_scan_params);
}

void ble_scan_start(discovery_handler handler)
{
    scan_handler = handler;

    esp_ble_gap_start_scanning(120);
}

void ble_scan_stop()
{
    esp_ble_gap_stop_scanning();
}

void ble_connect(uint8_t *address, int type)
{
    esp_bd_addr_t *esp_adr = (esp_bd_addr_t *)address;

    esp_ble_gattc_open(gatt_handle, *esp_adr, (esp_ble_addr_type_t)type, true);
}

void ble_disconnect()
{
    esp_ble_gattc_close(gatt_if, conn_id);
}

bool ble_write_char(uint16_t handle, uint8_t *data, int dataLength)
{
    ESP_LOGI(TAG, "ble_write_char %d %d", handle, dataLength);

    esp_err_t err = esp_ble_gattc_write_char(gatt_if, conn_id, handle, dataLength, data, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
    return (err == ESP_OK);
}

bool ble_write_char_secure(uint16_t handle, uint8_t *data, int dataLength)
{
    return (esp_ble_gattc_write_char(gatt_if, conn_id, handle, dataLength, data, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_SIGNED_MITM) == ESP_OK);
}

static void write_chr_desc(uint16_t service_start, uint16_t service_end, uint16_t handle, uint16_t value, bool safe)
{
    uint16_t count = 0;
    uint16_t offset = 0;

    esp_gatt_status_t ret_status = esp_ble_gattc_get_attr_count(gatt_if, conn_id, ESP_GATT_DB_DESCRIPTOR, service_start, service_end, handle, &count);
    if (ret_status != ESP_GATT_OK)
    {
        ESP_LOGE(TAG, "esp_ble_gattc_get_attr_count error, %d", __LINE__);
    }

    if (count > 0)
    {
        esp_gattc_descr_elem_t *descr_elem_result = malloc(sizeof(esp_gattc_descr_elem_t) * count);
        if (!descr_elem_result)
        {
            ESP_LOGE(TAG, "ble_enable_indication no mem");
        }
        else
        {
            ret_status = esp_ble_gattc_get_all_descr(gatt_if, conn_id, handle, descr_elem_result, &count, offset);
            if (ret_status != ESP_GATT_OK)
            {
                ESP_LOGE(TAG, "esp_ble_gattc_get_all_descr error, %d %x", __LINE__, ret_status);
            }

            for (int i = 0; i < count; ++i)
            {
                esp_bt_uuid_t cuuid = descr_elem_result[i].uuid;

                if (cuuid.len == ESP_UUID_LEN_16 && cuuid.uuid.uuid16 == 0x2902) // Characteristic settings
                {
                    // Register for notification
                    esp_err_t err = esp_ble_gattc_register_for_notify(gatt_if, remote_bda, handle);
                    if (err != ESP_OK)
                    {
                        ESP_LOGI(TAG, "esp_ble_gattc_register_for_notify FAIL %d", err);
                    }

                    ESP_LOGI(TAG, "Writing 0x2902");

                    // Write the indication flag
                    err = esp_ble_gattc_write_char_descr(gatt_if, conn_id, descr_elem_result[i].handle,
                                                         sizeof(value), (uint8_t *)&value,
                                                         ESP_GATT_WRITE_TYPE_RSP, safe ?  ESP_GATT_AUTH_REQ_SIGNED_MITM : ESP_GATT_AUTH_REQ_NONE);

                    if (err != ESP_OK)
                    {
                        ESP_LOGI(TAG, "esp_ble_gattc_write_char_descr FAIL %d", err);
                    }
                }
            }
        }
        free(descr_elem_result);
    }
    else
    {
        ESP_LOGE(TAG, "No descs got!");
    }
}

void ble_enable_indication(uint16_t service_start, uint16_t service_end, uint16_t handle)
{
    write_chr_desc(service_start, service_end, handle, BLE_INDICATION, false);
}

void ble_enable_notification(uint16_t service_start, uint16_t service_end, uint16_t handle, bool safe)
{
    write_chr_desc(service_start, service_end, handle, BLE_NOTIFICATION, safe);
}