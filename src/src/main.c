#include "main.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_bt.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2c.h"

#include "config.h"
#include "input.h"
#include "SSD1306.h"
#include "menu.h"
#include "app_ble.h"
#include "timer.h"

void main_input(int button)
{
    menu_input(button);
}

bool i2c_init()
{
    esp_err_t err;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = DISPLAY_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = DISPLAY_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = DISPLAY_I2C_FREQ};

    err = i2c_driver_install(DISPLAY_I2C, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK)
    {
        ESP_LOGI("I2C", "i2c_driver_install FAIL");

        return false;
    }

    err = i2c_param_config(DISPLAY_I2C, &conf);
    if (err != ESP_OK)
    {
        ESP_LOGI("I2C", "i2c_param_config FAIL");

        return false;
    }

    return true;
}

void display_init()
{
     SSD1306_begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADR, DISPLAY_I2C);

    SSD1306_clearDisplay();
    SSD1306_drawText(0, 0, "BOOTING", 2, WHITE);
    SSD1306_display();
}

void app_main()
{
    ESP_LOGI("MAIN", "[BOOT]");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    menu_init();

    i2c_init();
    display_init();

    input_init();

    ble_init();

    app_timer_init();

    menu_set(MENU_MAIN);
}
