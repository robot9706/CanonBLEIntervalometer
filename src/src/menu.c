#include "menu.h"
#include "main.h"
#include "SSD1306.h"
#include "input.h"
#include "app_ble.h"
#include "canon_ble.h"
#include "timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "MENU"

#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)

// Menu list
static uint8_t menulist_selected;
static uint8_t menulist_scroll;
static uint8_t menulist_count;
static char **menulist_items;

static void menulist_draw()
{
    SSD1306_clearDisplay();

    for (int drawY = 0; drawY < MIN(3, menulist_count); drawY++)
    {
        uint8_t realIndex = menulist_scroll + drawY;

        uint16_t y = 21 * drawY;

        if (menulist_selected == realIndex)
        {
            SSD1306_fillRect(0, y, SSD1306_LCDWIDTH, 18, WHITE);
        }
        if (menulist_items[realIndex] != NULL)
        {
            SSD1306_drawText(1, y + 2, menulist_items[realIndex], 2, (menulist_selected == realIndex ? BLACK : WHITE));
        }
    }

    SSD1306_display();
}

static void menulist_init(char **items, uint8_t count)
{
    menulist_selected = 0;
    menulist_scroll = 0;
    menulist_items = items;
    menulist_count = count;

    menulist_draw();
}

static int16_t menulist_input(uint8_t button)
{
    switch (button)
    {
    case INPUT_LEFT:
        if (menulist_selected == 0)
            menulist_selected = menulist_count - 1;
        else
            menulist_selected--;
        break;
    case INPUT_RIGHT:
        menulist_selected++;
        if (menulist_selected == menulist_count)
            menulist_selected = 0;
        break;
    case INPUT_BUTTON:
        return menulist_selected;
    }

    if (menulist_selected < menulist_scroll)
        menulist_scroll = menulist_selected;
    if (menulist_selected > menulist_scroll + 2)
        menulist_scroll = MAX(0, menulist_selected - 2);

    menulist_draw();

    return -1;
}

static void menu_camera_disconnect()
{
    menu_set(MENU_MAIN);
}

// Main menu
static char *menu_page0_items[] = {
    (char *)"Connect",
    (char *)"Pair"};

static void menu_page0_activate()
{
    menulist_init(menu_page0_items, 2);
}

static void menu_page0_input(uint8_t button)
{
    int16_t selected = menulist_input(button);
    if (selected != -1)
    {
        switch (selected)
        {
        case 0:
            menu_set(MENU_CONNECT);
            break;
        case 1:
            menu_set(MENU_PAIR);
            break;
        }
    }
}

// Pair page
struct ble_adr
{
    uint8_t address[6];
    int type;
};

#define MENU1_NAME_START 1
#define MENU1_NAME_COUNT 6
#define MENU1_NAME_LEN 16
static char **menu_page1_items = NULL;
static struct ble_adr menu_page1_addrs[MENU1_NAME_COUNT];
static uint8_t menu_page1_selected;

static void menu_page1_scancallback(const char *name, int len, esp_bd_addr_t addr, int addrType)
{
    bool changed = false;

    for (int x = MENU1_NAME_START; x < MENU1_NAME_START + MENU1_NAME_COUNT; x++)
    {
        if (strlen(menu_page1_items[x]) == 0)
        {
            // Empty slot, copy the name into here
            memset(menu_page1_items[x], 0, MENU1_NAME_LEN);
            strncpy(menu_page1_items[x], name, MIN(len, MENU1_NAME_LEN));

            memcpy(menu_page1_addrs[x - MENU1_NAME_START].address, &addr[0], 6);
            menu_page1_addrs[x - MENU1_NAME_START].type = addrType;

            changed = true;
            break;
        }
        else if (strcmp(menu_page1_items[x], name) == 0)
        {
            break; // Name already found
        }
    }

    if (changed)
    {
        menulist_draw();
    }
}

static void menu_page1_activate()
{
    if (menu_page1_items == NULL)
    {
        menu_page1_items = (char **)malloc((MENU1_NAME_COUNT + 1) * sizeof(char *));
        for (int i = 0; i < MENU1_NAME_COUNT + 1; i++)
            menu_page1_items[i] = malloc(MENU1_NAME_LEN * sizeof(char));

        strcpy(menu_page1_items[0], (const char *)"Back");
    }
    for (int x = MENU1_NAME_START; x < MENU1_NAME_START + MENU1_NAME_COUNT; x++)
    {
        strcpy(menu_page1_items[x], "");
    }

    menulist_init(menu_page1_items, 7);
    ble_scan_start(menu_page1_scancallback);
}

static void menu_page1_deactivate()
{
    ble_scan_stop();
}

static void menu_page1_input(uint8_t button)
{
    int16_t selected = menulist_input(button);
    if (selected != -1)
    {
        if (selected == 0)
        {
            menu_set(MENU_MAIN);
        }
        else
        {
            if (strlen(menu_page1_items[selected]) != 0)
            {
                menu_page1_selected = selected - 1;
                menu_set(MENU_PAIR_CONNECT);
            }
        }
    }
}

// Do PAIR page
#define PAGE2_STATE_WORKING (0)
#define PAGE2_STATE_OK (1)
#define PAGE2_STATE_FAIL (2)

static int menu_page2_state = PAGE2_STATE_WORKING;
static int mneu_page2_current = 0;
static char *menu_page2_states[] = {
    (char *)"Connecting",
    (char *)"Bonding", // PAIR_STATE_BOND
    (char *)"Request", // PAIR_STATE_REQUEST
    (char *)"Wait",    // PAIR_STATE_WAIT
    (char *)"Info",    // PAIR_STATE_INFO
    (char *)"Done"     // PAIR_STATE_DONE
};

static void menu_page2_draw()
{
    SSD1306_clearDisplay();
    if (menu_page2_state == PAGE2_STATE_FAIL)
    {
        SSD1306_drawText(0, 0, "Failed", 2, WHITE);
    }
    else
    {
        SSD1306_drawText(0, 0, menu_page2_states[mneu_page2_current], 2, WHITE);
    }

    SSD1306_drawText(0, 16, menu_page1_items[menu_page1_selected + 1], 2, WHITE);
    SSD1306_display();
}

static void menu_page2_camera_connected()
{
    ESP_LOGI(TAG, "Camera connected, start bonding and canon pair");

    mneu_page2_current = 1;
    canon_start_pair();

    menu_page2_draw();
}

static void menu_page2_camera_pair(int state, bool status)
{
    mneu_page2_current = state;

    if (!status)
    {
        menu_page2_state = PAGE2_STATE_FAIL;
    }

    if (state == PAIR_STATE_DONE)
    {
        menu_page2_state = PAGE2_STATE_OK;
    }

    menu_page2_draw();
}

static void menu_page2_activate()
{
    // Reset states
    menu_page2_state = PAGE2_STATE_WORKING;
    mneu_page2_current = 0;

    // Update UI
    menu_page2_draw();

    // Starting pair
    canon_set_on_connected(menu_page2_camera_connected);                                                        // Set the camera connect callback
    canon_set_pair_state_callback(menu_page2_camera_pair);                                                      // Set the pair state callback
    canon_set_on_disconnected(menu_camera_disconnect);                                                          // Set the disconnect handler
    ble_connect(&menu_page1_addrs[menu_page1_selected].address[0], menu_page1_addrs[menu_page1_selected].type); // Connect to the camera
}

static void menu_page2_input(uint8_t input)
{
    if (input == INPUT_BUTTON)
    {
        canon_set_on_disconnected(NULL);
        ble_disconnect();

        menu_set(MENU_PAIR);
    }
}

// Connect to camera page
static void menu_page3_input(uint8_t button)
{
    int16_t selected = menulist_input(button);
    if (selected != -1)
    {
        if (selected == 0)
        {
            menu_set(MENU_MAIN);
        }
        else
        {
            if (strlen(menu_page1_items[selected]) != 0)
            {
                menu_page1_selected = selected - 1;
                menu_set(MENU_CONNECT_DO);
            }
        }
    }
}

// DO connect page
static bool menu_page4_auth = false;

static void menu_page4_draw()
{
    // Draw a basic display
    SSD1306_clearDisplay();
    SSD1306_drawText(0, 0, menu_page4_auth ? "Auth" : "Connecting", 2, WHITE);
    SSD1306_drawText(0, 16, menu_page1_items[menu_page1_selected + 1], 2, WHITE);
    SSD1306_display();
}

static void menu_page4_camera_connected()
{
    menu_page4_auth = true;
    menu_page4_draw();

    canon_do_connect();
}

static void menu_page4_camera_auth()
{
    menu_set(MENU_CAMERA_MAIN);
}

static void menu_page4_activate()
{
    menu_page4_draw();

    // Start connecting
    canon_set_on_connected(menu_page4_camera_connected); // Set the camera connect callback
    canon_set_pair_state_callback(NULL);
    canon_set_on_disconnected(menu_camera_disconnect); // Set the disconnect handler
    canon_set_on_auth(menu_page4_camera_auth);
    ble_connect(&menu_page1_addrs[menu_page1_selected].address[0], menu_page1_addrs[menu_page1_selected].type); // Connect to the camera
}

static void menu_page4_input(uint8_t input)
{
    if (input == INPUT_BUTTON)
    {
        canon_set_on_disconnected(NULL);
        ble_disconnect();

        menu_set(MENU_CONNECT);
    }
}

// Camera main menu
static char *menu_page5_items[] = {
    (char *)"Trigger",
    (char *)"Timer",
    (char *)"Disconnect",
};

static void menu_page5_activate()
{
    menulist_init(menu_page5_items, 3);
}

static void menu_page5_input(uint8_t button)
{
    int16_t selected = menulist_input(button);
    if (selected != -1)
    {
        switch (selected)
        {
        case 0: // Trigger - Do one trigger
            canon_do_trigger();
            break;
        case 1: // Timer - goto the timer page
            menu_set(MENU_CAMERA_TIMER);
            break;
        case 2: // Disconnect - go back
            canon_set_on_disconnected(NULL);
            ble_disconnect(); // Make sure we disconnect from the camera

            menu_set(MENU_CONNECT);
            break;
        }
    }
}

// Timer menu
#define MENU_PAGE_6_TIME 0
#define MENU_PAGE_6_BACK 1
#define MENU_PAGE_6_START 2

#define MENU_PAGE_6_MAX 2
static int menu_page6_selected = MENU_PAGE_6_TIME;
static bool menu_page6_selected_active = false;

static int menu_page6_timer_interval = 5;
static bool menu_page6_timer_running = false;

static int menu_page6_timer_countdown;
static int menu_page6_expo_count;

static SemaphoreHandle_t menu_page6_display_semaphore = NULL;

static SemaphoreHandle_t menu_page6_timer_semaphore = NULL;
static bool menu_page6_timer_trigger = false;

static void menu_page6_draw();

static void menu_page6_timer_callback()
{
    ESP_LOGI(TAG, "Trigger");

    menu_page6_timer_trigger = true;
    xSemaphoreGive(menu_page6_timer_semaphore);
}

static void menu_page6_timer_task()
{
    while (true)
    {
        ESP_LOGI(TAG, "Waiting for trigger");
        while (xSemaphoreTake(menu_page6_timer_semaphore, portMAX_DELAY) != pdPASS)
        {
            // Do nothing
        }

        ESP_LOGI(TAG, "Got trigger");
        menu_page6_timer_trigger = false;
        xSemaphoreGive(menu_page6_timer_semaphore);
        xSemaphoreTake(menu_page6_timer_semaphore, portMAX_DELAY);

        menu_page6_timer_countdown--;
        if (menu_page6_timer_countdown <= 0)
        {
            menu_page6_timer_countdown = menu_page6_timer_interval;

            menu_page6_expo_count++;

            canon_do_trigger();
        }

        menu_page6_draw();
    }
}

static void menu_page6_init_timer_task()
{
    ESP_LOGI(TAG, "Page6 semaphore init");

    menu_page6_display_semaphore = xSemaphoreCreateMutex();
    menu_page6_timer_semaphore = xSemaphoreCreateBinary();

    menu_page6_timer_trigger = false;
    xSemaphoreTake(menu_page6_timer_semaphore, (TickType_t)20);

    ESP_LOGI(TAG, "Page6 task create");
    xTaskCreate(menu_page6_timer_task, "timer_task", 1024 * 8, NULL, 10, NULL);
}

static void menu_page6_timer_start()
{
    menu_page6_timer_countdown = menu_page6_timer_interval;

    app_timer_start(menu_page6_timer_callback);
    menu_page6_timer_running = true;
}

static void menu_page6_timer_stop()
{
    app_timer_stop();
    menu_page6_timer_running = false;
}

static void menu_page6_button(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char *text, bool selected)
{
    if (selected)
    {
        SSD1306_fillRect(x, y, w, h, WHITE);
    }
    else
    {
        SSD1306_outlineRect(x, y, w - 1, h - 1, WHITE);
    }

    int textlen = strlen(text);
    SSD1306_drawText((w / 2) - ((textlen * 12) / 2) + x, (h / 2) - (12 / 2) + y, text, 2, (selected ? BLACK : WHITE));
}

static void menu_page6_draw()
{
    if (menu_page6_display_semaphore != NULL)
    {
        // Take semaphore is possible, avoids issue when the input and the timer wants to redraw the UI at the same time
        if (xSemaphoreTake(menu_page6_display_semaphore, (TickType_t)20) == pdTRUE)
        {
            SSD1306_clearDisplay();

            // Interval setting
            {
                int textColor = ((menu_page6_selected == MENU_PAGE_6_TIME && menu_page6_selected_active) ? BLACK : WHITE);

                if (menu_page6_selected == MENU_PAGE_6_TIME && menu_page6_selected_active)
                {
                    SSD1306_fillRect(0, 0, SSD1306_LCDWIDTH, 19, WHITE);
                }

                SSD1306_drawText(2, 2, "Set:", 2, textColor);
                char intervalBuf[16];
                sprintf(intervalBuf, "%d", menu_page6_timer_interval);
                SSD1306_drawText(48, 2, intervalBuf, 2, textColor);

                if (menu_page6_selected == MENU_PAGE_6_TIME && !menu_page6_selected_active)
                {
                    SSD1306_drawFastHLine(0, 17, SSD1306_LCDWIDTH, WHITE);
                    SSD1306_drawFastHLine(0, 18, SSD1306_LCDWIDTH, WHITE);
                }
            }

            // Countdown
            if (menu_page6_timer_running)
            {
                char countdownBuffer[16];
                sprintf(countdownBuffer, "%ds", menu_page6_timer_countdown);

                int textlen = strlen(countdownBuffer);
                SSD1306_drawText((SSD1306_LCDWIDTH / 4) - ((textlen * 12) / 2), (12 / 2) + 18, countdownBuffer, 2, WHITE);
            }

            // Expo count
            {
                char expoBuffer[16];
                sprintf(expoBuffer, "%d", menu_page6_expo_count);

                int textlen = strlen(expoBuffer);
                SSD1306_drawText((SSD1306_LCDWIDTH / 2) + (SSD1306_LCDWIDTH / 4) - ((textlen * 12) / 2), (12 / 2) + 18, expoBuffer, 2, WHITE);
            }

            menu_page6_button(0, 43, SSD1306_LCDHEIGHT, 21, "Back", (menu_page6_selected == MENU_PAGE_6_BACK));
            menu_page6_button(64, 43, SSD1306_LCDHEIGHT, 21, (menu_page6_timer_running ? "Stop" : "Start"), (menu_page6_selected == MENU_PAGE_6_START));
            SSD1306_display();

            // Release the semaphore
            xSemaphoreGive(menu_page6_display_semaphore);
        }
    }
    else
    {
        ESP_LOGE(TAG, "PAGE6 semaphore null");
    }
}

static void menu_page6_activate()
{
    menu_page6_expo_count = 0;

    menu_page6_draw();
}

static void menu_page6_button_press()
{
    bool redraw = true;

    switch (menu_page6_selected)
    {
    case MENU_PAGE_6_TIME:
    {
        menu_page6_selected_active = !menu_page6_selected_active;
        break;
    }
    case MENU_PAGE_6_BACK:
    {
        redraw = false;
        menu_set(MENU_CAMERA_MAIN);
        break;
    }
    case MENU_PAGE_6_START:
    {
        menu_page6_timer_running = !menu_page6_timer_running;
        if (menu_page6_timer_running)
        {
            menu_page6_timer_start();
        }
        else
        {
            menu_page6_timer_stop();
        }
        break;
    }
    }

    if (redraw)
    {
        menu_page6_draw();
    }
}

static void menu_page6_time_input(uint8_t button)
{
    if (menu_page6_selected != MENU_PAGE_6_TIME)
    {
        menu_page6_selected_active = false;
        return;
    }

    switch (button)
    {
    case INPUT_BUTTON:
    {
        menu_page6_selected_active = false;
        break;
    }
    case INPUT_LEFT:
    {
        if (menu_page6_timer_interval > 1)
        {
            menu_page6_timer_interval--;
        }
        break;
    }
    case INPUT_RIGHT:
    {
        if (menu_page6_timer_interval < 60 * 60)
        {
            menu_page6_timer_interval++;
        }
        break;
    }
    }
}

static void menu_page6_input(uint8_t button)
{
    if (menu_page6_selected_active)
    {
        menu_page6_time_input(button);

        menu_page6_draw();
    }
    else
    {
        switch (button)
        {
        case INPUT_BUTTON:
        {
            menu_page6_button_press();
            break;
        }
        case INPUT_LEFT:
        {
            if (menu_page6_selected == 0)
                menu_page6_selected = MENU_PAGE_6_MAX;
            else
                menu_page6_selected--;

            menu_page6_draw();
            break;
        }
        case INPUT_RIGHT:
        {
            if (menu_page6_selected == MENU_PAGE_6_MAX)
                menu_page6_selected = 0;
            else
                menu_page6_selected++;

            menu_page6_draw();
            break;
        }
        }
    }
}

static void menu_page6_deactivate()
{
    if (menu_page6_timer_running)
    {
        menu_page6_timer_stop();
    }
}

// Menu manager
struct menu_page
{
    void (*activate)();
    void (*input)(uint8_t button);
    void (*deactivate)();
};

static uint8_t activeMenu;
static struct menu_page pages[] = {
    {.activate = menu_page0_activate, .input = menu_page0_input, .deactivate = NULL},                  // Main menu
    {.activate = menu_page1_activate, .input = menu_page1_input, .deactivate = menu_page1_deactivate}, // Pair menu
    {.activate = menu_page2_activate, .input = menu_page2_input, .deactivate = NULL},                  // Do pair menu
    {.activate = menu_page1_activate, .input = menu_page3_input, .deactivate = menu_page1_deactivate}, // Connect menu - Copy of Pair menu
    {.activate = menu_page4_activate, .input = menu_page4_input, .deactivate = NULL},                  // Do connect menu
    {.activate = menu_page5_activate, .input = menu_page5_input, .deactivate = NULL},                  // Camera main menu
    {.activate = menu_page6_activate, .input = menu_page6_input, .deactivate = menu_page6_deactivate}, // Timer menu
};

void menu_init()
{
    menu_page6_init_timer_task();
}

void menu_set(uint8_t index)
{
    if (pages[activeMenu].deactivate != NULL)
    {
        pages[activeMenu].deactivate();
    }

    activeMenu = index;

    if (pages[activeMenu].activate != NULL)
    {
        pages[activeMenu].activate();
    }
}

void menu_input(uint8_t button)
{
    if (pages[activeMenu].input != NULL)
    {
        pages[activeMenu].input(button);
    }
}
