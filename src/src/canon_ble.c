#include "canon_ble.h"

#define TAG "CAN"

static uint8_t pair_name[] = {0x01, 'T', 'I', 'M', 'E', 'R'};
static uint8_t pair_platform[] = {0x05, 0x02}; // Android
static uint8_t pair_confirm[] = {0x01};

static uint8_t trig_cfg[] = {0x03};
static uint8_t trig_seq0[] = {0x00, 0x01};
static uint8_t trig_seq1[] = {0x00, 0x02};

#define PAIR_ACCEPTED (0x02)

static uint8_t *get_char_data(int data_type, int *data_length);

// Forward declarations
static uint16_t get_char_handle(uint8_t can_type);
static void execute_current_command();

static void callback_pair(bool accepted);
static void callback_pair_complete(bool dontcare);

static void callback_camera_connect_auth(bool dontcare);

// Handlers
static simple_callback on_connected_handler = NULL;
static canon_pair_state_callback on_pair_state_handler = NULL;
static simple_callback on_disconnected_handler = NULL;
static simple_callback on_auth_handler = NULL;

void canon_set_on_connected(simple_callback handler)
{
    on_connected_handler = handler;
}

void canon_set_pair_state_callback(canon_pair_state_callback handler)
{
    on_pair_state_handler = handler;
}

void canon_set_on_disconnected(simple_callback handler)
{
    on_disconnected_handler = handler;
}

void canon_set_on_auth(simple_callback handler)
{
    on_auth_handler = handler;
}

// Command system
#define BLE_CMD_NONE (0)
#define BLE_CMD_WRITE (1)
#define BLE_CMD_WRITE_SECURE_BOND (2)
#define BLE_CMD_ENABLE_INDICATION (3)
#define BLE_CMD_WAIT_INDICATION (4)
#define BLE_CMD_ENABLE_NOTIFICATION (5)
#define BLE_CMD_ENABLE_NOTIFICATION_SAFE (6)

#define CAN_CHR_NONE (0)
#define CAN_CHR_PAIR_COMMAND (1)
#define CAN_CHR_PAIR_DATA (2)
#define CAN_CHR_TRIG (10)
#define CAN_CHR_TRIG_NOTIF (11)
#define CAN_CHR_TRIG_CONFIG (12)

#define CAN_DATA_NONE (0)
#define CAN_DATA_NAME (1)
#define CAN_DATA_PLATFORM (2)
#define CAN_DATA_CONFIRM (3)
#define CAN_DATA_TRIG0 (10)
#define CAN_DATA_TRIG1 (11)
#define CAN_DATA_CONFIG (12)

struct canon_command
{
    uint8_t ble_type;
    uint8_t can_chr;
    uint8_t can_data;
};

struct canon_commandset
{
    uint8_t id;
    uint8_t num;
    struct canon_command *set;
    simple_statue_callback on_done;
};

static struct canon_command cmdset_pair_request[] = {
    // Send the name to the camera, to bond, this actually won't get executed, it is only used to create a bond
    {.ble_type = BLE_CMD_WRITE_SECURE_BOND, .can_chr = CAN_CHR_PAIR_COMMAND, .can_data = CAN_DATA_NAME},

    // Send the name to the camera
    {.ble_type = BLE_CMD_WRITE, .can_chr = CAN_CHR_PAIR_COMMAND, .can_data = CAN_DATA_NAME},

    // Enable the indication
    {.ble_type = BLE_CMD_ENABLE_INDICATION, .can_chr = CAN_CHR_PAIR_COMMAND, .can_data = CAN_DATA_NONE},

    // Wait for the accept/deny result
    {.ble_type = BLE_CMD_WAIT_INDICATION, .can_chr = CAN_CHR_PAIR_COMMAND, .can_data = CAN_DATA_NONE}};

static struct canon_command cmdset_pair_info[] = {
    // Send the name to the camera
    {.ble_type = BLE_CMD_WRITE, .can_chr = CAN_CHR_PAIR_DATA, .can_data = CAN_DATA_NAME},

    // Send the platform to the camera
    {.ble_type = BLE_CMD_WRITE, .can_chr = CAN_CHR_PAIR_DATA, .can_data = CAN_DATA_PLATFORM},

    // Send the confirmation to the camera
    {.ble_type = BLE_CMD_WRITE, .can_chr = CAN_CHR_PAIR_DATA, .can_data = CAN_DATA_CONFIRM},
};

static struct canon_command cmdset_connect[] = {
    // Enable notification on the trigger callback - this will fail and enables bonding
    {.ble_type = BLE_CMD_ENABLE_NOTIFICATION_SAFE, .can_chr = CAN_CHR_TRIG_NOTIF, .can_data = CAN_DATA_NONE},

    // Enable notification on the trigger callback for real this time
    {.ble_type = BLE_CMD_ENABLE_NOTIFICATION, .can_chr = CAN_CHR_TRIG_NOTIF, .can_data = CAN_DATA_NONE},

    {.ble_type = BLE_CMD_WRITE, .can_chr = CAN_CHR_TRIG_CONFIG, .can_data = CAN_DATA_CONFIG}};

static struct canon_command cmdset_trigger[] = {
    // Send the trigger sequence
    {.ble_type = BLE_CMD_WRITE, .can_chr = CAN_CHR_TRIG, .can_data = CAN_DATA_TRIG0},

    // Send the trigger finish
    {.ble_type = BLE_CMD_WRITE, .can_chr = CAN_CHR_TRIG, .can_data = CAN_DATA_TRIG1},
};

#define CMD_PAIR (0)
#define CMD_PAIR_INFO (1)
#define CMD_CONNECT (2)
#define CMD_TRIGGER (3)

#define CMDSET_PAIR_REQUEST                                                            \
    {                                                                                  \
        .id = CMD_PAIR, .num = 4, .set = cmdset_pair_request, .on_done = callback_pair \
    }
#define CMDSET_PAIR_INFO                                                                          \
    {                                                                                             \
        .id = CMD_PAIR_INFO, .num = 3, .set = cmdset_pair_info, .on_done = callback_pair_complete \
    }
#define CMDSET_CONNECT                                                                              \
    {                                                                                               \
        .id = CMD_CONNECT, .num = 3, .set = cmdset_connect, .on_done = callback_camera_connect_auth \
    }
#define CMDSET_TRIGGER                                                      \
    {                                                                       \
        .id = CMD_TRIGGER, .num = 2, .set = cmdset_trigger, .on_done = NULL \
    }

static struct canon_command *active_cmdset = NULL;
static uint8_t command_id;
static uint8_t current_command;
static uint8_t num_command;
static simple_statue_callback ondone_cmdset = NULL;

static void execute_command_set(struct canon_commandset cmdset)
{
    ESP_LOGI(TAG, "Executing command set %d", cmdset.id);

    active_cmdset = cmdset.set;
    command_id = cmdset.id;
    current_command = 0;
    num_command = cmdset.num;
    ondone_cmdset = cmdset.on_done;

    execute_current_command();
}

// Services and characteristics
struct canon_service
{
    uint16_t start_handle;
    uint16_t end_handle;
};

static struct canon_service pair_service;
static struct canon_service trigger_service;

static uint16_t pair_service_chars[2] = {0};
static uint16_t trigger_service_chars[3] = {0};

static void execute_next()
{
    current_command++;
    if (current_command < num_command)
    {
        ESP_LOGI(TAG, "CommandSet NEXT");

        execute_current_command();
    }
    else
    {
        ESP_LOGI(TAG, "CommandSet DONE");

        if (ondone_cmdset != NULL)
        {
            ondone_cmdset(true);
        }
    }
}

static void execute_current_command()
{
    struct canon_command current = active_cmdset[current_command];

    switch (current.ble_type)
    {
    case BLE_CMD_WRITE:
    {
        ESP_LOGI(TAG, "Executing command BLE_CMD_WRITE");

        uint16_t handle = get_char_handle(current.can_chr);

        int length = 0;
        uint8_t *data_pointer = get_char_data(current.can_data, &length);

        ESP_LOGI(TAG, "WRITE %d %d", handle, length);

        if (data_pointer == NULL)
        {
            ESP_LOGI(TAG, "NULL data to write!");
        }
        else
        {
            // Write the characteristic
            if (!ble_write_char(handle, data_pointer, length))
            {
                ESP_LOGI(TAG, "BLE_CMD_WRITE fail");
            }
        }
        break;
    }
    case BLE_CMD_WRITE_SECURE_BOND:
    {
        ESP_LOGI(TAG, "Executing command BLE_CMD_WRITE_SECURE_BOND");

        uint16_t handle = get_char_handle(current.can_chr);

        int length = 0;
        uint8_t *data_pointer = get_char_data(current.can_data, &length);

        if (data_pointer == NULL)
        {
            ESP_LOGI(TAG, "NULL data to write!");
        }
        else
        {
            ble_write_char_secure(handle, data_pointer, length); // Write the characteristic secure, this initiates bonding
        }
        break;
    }
    case BLE_CMD_ENABLE_INDICATION:
    {
        ESP_LOGI(TAG, "Executing command BLE_CMD_ENABLE_INDICATION");

        uint16_t handle = get_char_handle(current.can_chr);

        ble_enable_indication(pair_service.start_handle, pair_service.end_handle, handle);

        break;
    }
    case BLE_CMD_ENABLE_NOTIFICATION:
    case BLE_CMD_ENABLE_NOTIFICATION_SAFE:
    {
        ESP_LOGI(TAG, "Executing command BLE_CMD_ENABLE_NOTIFICATION");

        uint16_t handle = get_char_handle(current.can_chr);

        ble_enable_notification(pair_service.start_handle, pair_service.end_handle, handle, (current.ble_type == BLE_CMD_ENABLE_NOTIFICATION_SAFE));

        break;
    }
    }
}

static uint16_t get_char_handle(uint8_t can_type)
{
    switch (can_type)
    {
    case CAN_CHR_PAIR_COMMAND:
        return pair_service_chars[0];
    case CAN_CHR_PAIR_DATA:
        return pair_service_chars[1];
    case CAN_CHR_TRIG:
        return trigger_service_chars[0];
    case CAN_CHR_TRIG_NOTIF:
        return trigger_service_chars[1];
    case CAN_CHR_TRIG_CONFIG:
        return trigger_service_chars[2];
    }

    return 0;
}

static uint8_t *get_char_data(int data_type, int *data_length)
{
    switch (data_type)
    {
    case CAN_DATA_NAME:
    {
        *data_length = sizeof(pair_name);
        return pair_name;
    }
    case CAN_DATA_PLATFORM:
    {
        *data_length = sizeof(pair_platform);
        return pair_platform;
    }
    case CAN_DATA_CONFIRM:
    {
        *data_length = sizeof(pair_confirm);
        return pair_confirm;
    }
    case CAN_DATA_TRIG0:
    {
        *data_length = sizeof(trig_seq0);
        return trig_seq0;
    }
    case CAN_DATA_TRIG1:
    {
        *data_length = sizeof(trig_seq1);
        return trig_seq1;
    }
    case CAN_DATA_CONFIG:
    {
        *data_length = sizeof(trig_cfg);
        return trig_cfg;
    }
    }

    data_length = 0;
    return NULL;
}

void canon_service_discovery(esp_bt_uuid_t uuid, uint16_t startHandle, uint16_t endHandle)
{
    // Is this the PAIR service?
    const uint8_t pairServiceUUID[] = {CANON_PAIR_SERVICE};
    if (uuid.len == ESP_UUID_LEN_128 && memcmp(uuid.uuid.uuid128, pairServiceUUID, ESP_UUID_LEN_128) == 0)
    {
        pair_service.start_handle = startHandle;
        pair_service.end_handle = endHandle;

        ESP_LOGI(TAG, "PAIR service found");
    }

    // Is this the TRIGGER service?
    const uint8_t triggerServiceUUID[] = {CANON_TRIG_SERVICE};
    if (uuid.len == ESP_UUID_LEN_128 && memcmp(uuid.uuid.uuid128, triggerServiceUUID, ESP_UUID_LEN_128) == 0)
    {
        trigger_service.start_handle = startHandle;
        trigger_service.end_handle = endHandle;

        ESP_LOGI(TAG, "TRIGGER service found");
    }
}

void canon_discovery_complete(esp_gatt_if_t gatt_if)
{
    ESP_LOGI(TAG, "Discovery complete");

    // Find PAIR characteristics
    uint8_t pair_findUUIDs[ESP_UUID_LEN_128 * 2] = {
        CANON_PAIR_COMMAND_CHARACTERISTIC,
        CANON_PAIR_DATA_CHARACTERISTIC};

    int result = ble_get_chars(gatt_if, pair_service.start_handle, pair_service.end_handle, pair_findUUIDs, 2, pair_service_chars);
    if (result != 2)
    {
        ESP_LOGI(TAG, "Failed to find PAIR characteristics!");
        return;
    }

    // Find TRIGGER characteristics
    uint8_t trig_findUUIDs[ESP_UUID_LEN_128 * 3] = {
        CANON_TRIG_CHARACTERISTIC,
        CANON_TRIG_NOTIFICATION_CHARACTERISTIC,
        CANON_TRIG_CONFIG_CHARACTERISTIC};

    result = ble_get_chars(gatt_if, trigger_service.start_handle, trigger_service.end_handle, trig_findUUIDs, 3, trigger_service_chars);
    if (result != 3)
    {
        ESP_LOGI(TAG, "Failed to find TRIGGER characteristics!");
        return;
    }

    ESP_LOGI(TAG, "Characteristics found");

    // The discovery is complete ready to communicate with the camera
    on_connected_handler();
}

void canon_bond_result(bool success)
{
    // Re-execute the current command, but now no security is required
    if (active_cmdset[current_command].ble_type == BLE_CMD_WRITE_SECURE_BOND)
    {
        if (command_id == CMD_PAIR)
        {
            on_pair_state_handler(PAIR_STATE_BOND, success);
        }

        if (success)
        {
            execute_next();
        }
    }
    else if (active_cmdset[current_command].ble_type == BLE_CMD_ENABLE_NOTIFICATION_SAFE)
    {
        if (command_id == CMD_CONNECT && success)
        {
            execute_next();
        }
    }
}

void canon_char_write_result(bool success)
{
    if (active_cmdset[current_command].ble_type == BLE_CMD_WRITE)
    {
        if (command_id == CMD_PAIR)
        {
            on_pair_state_handler(PAIR_STATE_REQUEST, success);
        }

        if (success)
        {
            execute_next();
        }
    }
}

void canon_chardesc_write_result(bool success)
{
    if (active_cmdset[current_command].ble_type == BLE_CMD_ENABLE_INDICATION)
    {
        if (command_id == CMD_PAIR)
        {
            on_pair_state_handler(PAIR_STATE_WAIT, success);
        }

        if (success)
        {
            execute_next();
        }
    }
    else if (active_cmdset[current_command].ble_type == BLE_CMD_ENABLE_NOTIFICATION)
    {
        if (command_id == CMD_CONNECT && success)
        {
            execute_next();
        }
    }
}

void canon_char_notify(uint16_t handle, uint8_t *data, uint16_t data_len)
{
    if (active_cmdset[current_command].ble_type == BLE_CMD_WAIT_INDICATION)
    {
        bool pair_result = (data_len >= 1 && data[0] == PAIR_ACCEPTED);

        ESP_LOGI(TAG, "PAIR result %d", pair_result);

        if (command_id == CMD_PAIR)
        {
            on_pair_state_handler(PAIR_STATE_INFO, pair_result);

            // Pairing is done
            ondone_cmdset(pair_result);
        }
    }
}

void canon_start_pair()
{
    ESP_LOGI(TAG, "canon_start_pair");

    struct canon_commandset cmd = CMDSET_PAIR_REQUEST;
    execute_command_set(cmd);
}

static void callback_pair(bool accepted)
{
    ESP_LOGI(TAG, "callback_pair %d", accepted);

    if (accepted) // If pairing is accepted, send the info required by the camera
    {
        struct canon_commandset cmd = CMDSET_PAIR_INFO;
        execute_command_set(cmd);
    }
}

static void callback_pair_complete(bool dontcare)
{
    ESP_LOGI(TAG, "CANON PAIR DONE!");

    if (command_id == CMD_PAIR_INFO)
    {
        on_pair_state_handler(PAIR_STATE_DONE, true);
    }
}

void canon_disconnect()
{
    if (on_disconnected_handler != NULL)
    {
        on_disconnected_handler();
    }
}

void canon_do_connect()
{
    struct canon_commandset cmd = CMDSET_CONNECT;
    execute_command_set(cmd);
}

static void callback_camera_connect_auth(bool dontcare)
{
    if (on_auth_handler != NULL)
    {
        on_auth_handler();
    }
}

void canon_do_trigger()
{
    struct canon_commandset cmd = CMDSET_TRIGGER;
    execute_command_set(cmd);
}
